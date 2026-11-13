"""Car-queue driver: scripted vehicles reacting to equipment in virtual time.

The driver is an environment stimulus, like a scenario's typed actions. It
never sets motor outputs, barrier position, ADC codes or firmware state: it
configures the camera/terminal (paused, trusted configuration) and submits the
same allowlisted vehicle/beam actions a person would. It decides only at its
own wake ticks (a 100 ms grid plus planned command ticks), so a run behaves
identically whatever step size or pacing the caller uses.

Each lane serves one car at a time; the lanes run concurrently. A car enters,
parks for its configured virtual seconds, then queues for the exit lane.
Every attempt produces a verdict: the outcome the course contract expects
for the car's profile, compared with what the equipment actually observed.
"""

import collections
import random
import re

from scenario import parser

PROFILE = {
    "entry_capture": ("normal", "retry_recovers", "failed", "no_reply"),
    "exit_capture": ("normal", "misread"),
    "payment": ("paid", "delayed", "cancelled", "malformed", "no_reply"),
    "quirk": ("none", "hesitate", "leave_during_payment", "linger_in_beam", "block_closing"),
}
DEFAULT_PROFILE = {"entry_capture": "normal", "exit_capture": "normal",
                   "payment": "paid", "quirk": "none"}
PARK_S = (1, 120)
DEFAULT_PARK_S = (3, 15)
MAX_CARS = 64
POLL = 10                    # ticks: the driver's 100 ms decision grid
SPEED = 2000                 # mm/s; 20 mm per 10 ms tick
STOP_MM = 800                # identification point, as in the fixtures
FAR_MM = 6000
HESITATE_MM = 2500           # code 1706: present, not yet identifying
HESITATE_TICKS = 200
WAIT_TICKS = {"entry": 1000, "exit": 1500}   # identification (+ payment) budget
CLEAR_TICKS = 1000           # barrier must be closed again within 10 s
LINGER_TICKS = 1200          # beyond the 10 s passage budget
DISPATCH_GAP = 100           # one second between consecutive cars in a lane
CAMERA = {
    "normal": ([93], False), "retry_recovers": ([60, 93], False),
    "failed": ([40], False), "no_reply": ([93], True),
}
TERMINAL = {
    "paid": (10, 8, "PAID", False), "delayed": (150, 2, "PAID", False),
    "cancelled": (10, 8, "CANCELLED", False), "malformed": (10, 8, "MALFORMED", False),
    "no_reply": (10, 8, "PAID", True),
}
# A car that drives away needs the reply to arrive after it has left.
ABANDON_TERMINAL = (300, 8, "PAID", False)
ASSISTANCE = {"failed", "no_reply", "misread", "full"}


def travel(distance_mm):
    return (abs(distance_mm) * 100 + SPEED - 1) // SPEED


def validate_profile(value, partial=False):
    parser.require(type(value) is dict, "profile must be an object")
    allowed = set(PROFILE) | {"plate", "park_s"}
    parser.require(value.keys() <= allowed, f"unknown profile fields: {value.keys() - allowed}")
    for key, choices in PROFILE.items():
        if key in value:
            parser.require(value[key] in choices, f"invalid {key}")
    if "park_s" in value:
        parser.integer(value["park_s"], *PARK_S)
    if "plate" in value:
        parser.require(type(value["plate"]) is str and
                       re.fullmatch(parser.PLATE, value["plate"]), "invalid plate")
    return value


class Car:
    def __init__(self, ident, plate, profile, park_s):
        self.id = ident
        self.plate = plate
        self.profile = {**DEFAULT_PROFILE, **profile}
        self.park_s = park_s
        self.state = "queued"
        self.spot = None
        self.ready_tick = 0
        self.attempts = 0
        self.note = ""

    def view(self, now):
        return {"id": self.id, "plate": self.plate, "profile": dict(self.profile),
                "park_s": self.park_s, "state": self.state, "spot": self.spot,
                "remaining_s": max(0, (self.ready_tick - now) / 100)
                if self.state == "parked" else None, "note": self.note}


class Visit:
    """One car's current use of one lane."""

    def __init__(self, car, lane, start):
        self.car = car
        self.lane = lane
        self.phase = "approach"
        self.until = start
        self.deadline = None
        self.buzzer = 0
        self.charges = 0
        self.full_seen = False
        self.linger = False
        self.outcome = None
        self.plate = car.plate


class Driver:
    def __init__(self, seed=1, capacity=4, cars=None):
        parser.integer(capacity, 1, 8)
        self.random = random.Random(seed)
        self.capacity = capacity
        self.cars = collections.OrderedDict()
        self.next_id = 1
        self.visits = {"entry": None, "exit": None}
        self.lane_free_at = {"entry": 0, "exit": 0}
        self.halted = {}
        # The deque only feeds the view; totals keep every verdict of the run.
        self.verdicts = collections.deque(maxlen=256)
        self.totals = {"pass": 0, "fail": 0}
        self.ctl = None
        for profile in cars if cars is not None else [{} for _ in range(5)]:
            self.add(profile)

    # ----- queue API (validated; enums only) -----------------------------
    def _plate(self, avoid=()):
        taken = {c.plate for c in self.cars.values()} | set(avoid)
        while True:
            letters = "".join(self.random.choice("ABCDEFGHJKLMNPRSTUVWXYZ") for _ in range(3))
            plate = (letters + str(self.random.randrange(10)) +
                     self.random.choice("ABCDEFGHJKLMNPRSTUVWXYZ0123456789") +
                     f"{self.random.randrange(100):02d}")
            if plate not in taken:
                return plate

    def add(self, profile):
        validate_profile(profile)
        parser.require(len(self.cars) < MAX_CARS, "car queue is full")
        plate = profile.get("plate") or self._plate()
        parser.require(all(c.plate != plate for c in self.cars.values()), "plate already queued")
        park = profile.get("park_s", self.random.randint(*DEFAULT_PARK_S))
        car = Car(self.next_id, plate, {k: v for k, v in profile.items() if k in PROFILE}, park)
        self.cars[car.id] = car
        self.next_id += 1
        return car

    def _car(self, ident):
        parser.require(type(ident) is int and ident in self.cars, "unknown car")
        return self.cars[ident]

    def update(self, ident, profile):
        car = self._car(ident)
        validate_profile(profile)
        if car.state != "queued":
            # Once inside, only the not-yet-happened exit behaviour can change.
            parser.require(car.state in ("parked", "stuck") and
                           profile.keys() <= {"exit_capture", "payment", "quirk", "park_s"},
                           "this car has already started; only its exit can change")
            parser.require(profile.get("quirk") in (None, "none", "leave_during_payment",
                                                    "hesitate"),
                           "entry quirks no longer apply")
        if "plate" in profile:
            parser.require(all(c.plate != profile["plate"] for c in self.cars.values()
                               if c is not car), "plate already queued")
            car.plate = profile["plate"]
        car.profile.update({k: v for k, v in profile.items() if k in PROFILE})
        if "park_s" in profile:
            car.park_s = profile["park_s"]
        return car

    def remove(self, ident):
        car = self._car(ident)
        parser.require(car.state in ("queued", "gone", "refused"),
                       "only queued or departed cars can be removed")
        del self.cars[ident]

    def retry(self, ident, payment):
        car = self._car(ident)
        parser.require(car.state == "stuck", "only a stuck car can retry its exit")
        parser.require(payment in PROFILE["payment"], "invalid payment")
        car.profile.update(payment=payment, exit_capture="normal", quirk="none")
        car.state = "exit_queued"
        car.ready_tick = self.ctl.tick if self.ctl else 0
        car.note = "retrying exit"
        return car

    def restart(self):
        """Reset is a lab restart: every configured car queues again."""
        cars = [{**c.profile, "plate": c.plate, "park_s": c.park_s} for c in self.cars.values()]
        return Driver(seed=self.random.randrange(1, 2**31), capacity=self.capacity, cars=cars)

    def view(self):
        now = self.ctl.tick if self.ctl else 0
        return {"cars": [c.view(now) for c in self.cars.values()],
                "verdicts": list(self.verdicts), "totals": dict(self.totals),
                "halted": dict(self.halted),
                "inside": self.inside(), "capacity": self.capacity,
                "lanes": {lane: None if v is None else {"car": v.car.id, "phase": v.phase}
                          for lane, v in self.visits.items()}}

    # ----- runner hooks --------------------------------------------------
    def attach(self, ctl):
        self.ctl = ctl

    def lot_full(self, ctl):
        """Occupancy as the lot oracle sees it: sessions open and close at the
        beam transitions, not when the driver's manoeuvre finishes."""
        return len(ctl.lot.sessions) >= self.capacity

    def inside(self):
        return sum(c.state in ("parked", "exit_queued", "exiting", "stuck")
                   for c in self.cars.values())

    def wake_ticks(self):
        now = self.ctl.tick
        grid = now - now % POLL + POLL
        wakes = [v.until for v in self.visits.values() if v and v.until > now]
        if any(self.visits.values()) or self._waiting():
            wakes.append(grid)
        return wakes

    def _waiting(self):
        return any(c.state in ("queued", "exit_queued") or c.state == "parked"
                   for c in self.cars.values())

    def _due(self, now):
        return now % POLL == 0 or any(v and v.until == now for v in self.visits.values())

    def before_step(self, ctl):
        now = ctl.tick
        if not self._due(now):
            return
        for lane in ("exit", "entry"):
            if lane in self.halted:
                continue
            visit = self.visits[lane]
            if visit is not None:
                self._advance(ctl, visit, now)
            if self.visits[lane] is None:
                self._dispatch(ctl, lane, now)

    def after_step(self, ctl, new, lot_events):
        now = ctl.tick
        for car in self.cars.values():
            if car.state == "parked" and now >= car.ready_tick:
                car.state = "exit_queued"

    # ----- lane behaviour ------------------------------------------------
    def _verdict(self, visit, observed, expected, now, reason=""):
        status = "pass" if observed in expected else "fail"
        self.totals[status] += 1
        car = visit.car
        self.verdicts.append({"car": car.id, "plate": visit.plate, "lane": visit.lane,
                              "tick": now, "expected": sorted(expected),
                              "observed": observed, "status": status, "reason": reason})
        car.note = f"{visit.lane}: {observed} ({status})"
        return status

    def _dispatch(self, ctl, lane, now):
        if now < self.lane_free_at[lane]:
            return
        if lane == "entry":
            car = next((c for c in self.cars.values() if c.state == "queued"), None)
        else:
            waiting = [c for c in self.cars.values() if c.state == "exit_queued"]
            car = min(waiting, key=lambda c: (c.ready_tick, c.id), default=None)
        if car is None:
            return
        visit = Visit(car, lane, now)
        car.attempts += 1
        car.state = "entering" if lane == "entry" else "exiting"
        if lane == "entry":
            confidences, missing = CAMERA[car.profile["entry_capture"]]
        else:
            confidences, missing = [93], False
            if car.profile["exit_capture"] == "misread":
                visit.plate = self._plate(avoid=[car.plate])
            delay, fragment, result, held = (
                ABANDON_TERMINAL if car.profile["quirk"] == "leave_during_payment"
                else TERMINAL[car.profile["payment"]])
            ctl.configure(lane, "payment", {"delay_ticks": delay, "fragment_bytes": fragment,
                                            "result": result, "missing": held})
        ctl.configure(lane, "camera", {"plate": visit.plate, "confidences": confidences,
                                       "delay_ticks": 10, "fragment_bytes": 8,
                                       "missing": missing})
        at = now + 1
        ctl.submit(lane, "vehicle.speed", -SPEED, at)
        if car.profile["quirk"] == "hesitate":
            at += travel(FAR_MM - HESITATE_MM)
            ctl.submit(lane, "vehicle.speed", 0, at)
            at += HESITATE_TICKS
            ctl.submit(lane, "vehicle.speed", -SPEED, at)
            at += travel(HESITATE_MM - STOP_MM)
        else:
            at += travel(FAR_MM - STOP_MM)
        ctl.submit(lane, "vehicle.speed", 0, at)
        visit.until = at
        self.visits[lane] = visit

    def _expected(self, visit):
        car, lane = visit.car, visit.lane
        if lane == "entry":
            if car.profile["entry_capture"] in ("failed", "no_reply"):
                return {"refused"}, car.profile["entry_capture"]
            if visit.full_seen:
                return {"refused", "entered"}, "full"
            return {"entered"}, "valid"
        if car.profile["exit_capture"] == "misread":
            return {"refused"}, "misread"
        if car.profile["quirk"] == "leave_during_payment":
            return {"abandoned"}, "abandon"
        if car.profile["payment"] in ("cancelled", "malformed", "no_reply"):
            return {"refused"}, car.profile["payment"]
        return {"exited"}, "paid"

    def _advance(self, ctl, visit, now):
        lane, car = visit.lane, visit.car
        state = ctl.lane_state(lane)
        physical, protocol = state["physical"], state["protocol"]
        monitor = ctl.monitors[lane]
        if visit.phase == "approach":
            if now < visit.until:
                return
            visit.phase = "wait"
            visit.deadline = now + WAIT_TICKS[lane]
            visit.buzzer = monitor.counts["buzzer"]
            visit.charges = len(monitor.charges)
            visit.full_seen = lane == "entry" and self.lot_full(ctl)
            return
        if visit.phase == "wait":
            visit.full_seen |= lane == "entry" and self.lot_full(ctl)
            expected, why = self._expected(visit)
            if why == "full" and not visit.full_seen:
                expected = {"entered"}
            opened = protocol["green"] and physical["position_mdeg"] == 90000
            buzzed = monitor.counts["buzzer"] > visit.buzzer
            if opened:
                observed = "entered" if lane == "entry" else "exited"
                if lane == "entry" and why == "full" and self.lot_full(ctl):
                    expected = {"refused"}
                self._verdict(visit, observed, expected, now,
                              "opened for " + visit.plate)
                self._pass(ctl, visit, now)
            elif (car.profile["quirk"] == "leave_during_payment" and lane == "exit" and
                  len(monitor.charges) > visit.charges):
                self._verdict(visit, "abandoned", expected, now, "drove away during payment")
                self._back_off(ctl, visit, now, "abandoned")
            elif buzzed or now >= visit.deadline:
                why_needs_buzzer = (why in ASSISTANCE) and not buzzed
                observed = "refused" if not why_needs_buzzer else "ignored"
                self._verdict(visit, observed, expected, now,
                              "assistance requested" if buzzed else "no opening before deadline")
                self._back_off(ctl, visit, now, "refused")
            return
        if visit.phase == "pass":
            if visit.linger and monitor.counts["buzzer"] > visit.buzzer:
                visit.linger = False
                self._verdict(visit, "assisted", {"assisted"}, now,
                              "car held the beam beyond the passage budget")
            if now < visit.until:
                return
            if visit.linger:
                visit.linger = False
                self._verdict(visit, "ignored", {"assisted"}, now,
                              "car held the beam beyond the passage budget")
            visit.phase = "clear"
            visit.deadline = now + CLEAR_TICKS
            if lane == "entry":
                car.state = "parked"
                car.ready_tick = now + car.park_s * 100
                used = {c.spot for c in self.cars.values() if c.spot is not None}
                car.spot = next((s for s in range(self.capacity) if s not in used), None)
            else:
                car.state = "gone"
                car.spot = None
            return
        if visit.phase == "clear":
            closed = (physical["position_mdeg"] == 0 and not physical["enable"]
                      and not protocol["green"])
            closing = physical["enable"] and not physical["direction"]
            if car.profile["quirk"] == "block_closing" and lane == "entry" and closing:
                ctl.submit(lane, "sensor.obstruction", 1)
                car.profile["quirk"] = "none"
                visit.phase = "blocked"
                visit.buzzer = monitor.counts["buzzer"]
                visit.deadline = now + 300
                return
            if closed:
                ctl.submit(lane, "vehicle.next", 1)
                self._finish(visit, now)
            elif now >= visit.deadline:
                self.halted[lane] = "barrier did not close; reset required"
                self._finish(visit, now)
            return
        if visit.phase == "blocked":
            buzzed = monitor.counts["buzzer"] > visit.buzzer
            if buzzed or now >= visit.deadline:
                self._verdict(visit, "assisted" if buzzed else "ignored", {"assisted"}, now,
                              "obstruction while closing")
                self.halted[lane] = "obstruction stopped the barrier; reset required"
                self._finish(visit, now)
            return
        if visit.phase == "back_off":
            if visit.outcome == "abandoned" and protocol["green"]:
                visit.outcome = "reported"
                self._verdict(visit, "opened_after_abandon", {"abandoned"}, now,
                              "barrier opened after the car drove away")
            if now >= visit.until:
                self._finish(visit, now)

    def _pass(self, ctl, visit, now):
        """Drive through: beam active, then clear once past the barrier."""
        lane = visit.lane
        plan = [(1, "vehicle.speed", -SPEED), (31, "sensor.obstruction", 1)]
        if visit.car.profile["quirk"] == "linger_in_beam" and lane == "entry":
            # Stop under the beam, beyond the 10 s passage budget, then leave.
            resume = 41 + LINGER_TICKS
            plan += [(41, "vehicle.speed", 0), (resume, "vehicle.speed", -SPEED),
                     (resume + 80, "sensor.obstruction", 0), (resume + 121, "vehicle.speed", 0)]
            visit.linger = True
            visit.buzzer = ctl.monitors[lane].counts["buzzer"]
            visit.car.profile["quirk"] = "none"
        else:
            plan += [(131, "sensor.obstruction", 0), (152, "vehicle.speed", 0)]
        for offset, signal, value in plan:
            ctl.submit(lane, signal, value, now + offset)
        visit.phase = "pass"
        visit.until = now + plan[-1][0]

    def _back_off(self, ctl, visit, now, outcome):
        lane, car = visit.lane, visit.car
        ctl.submit(lane, "vehicle.speed", SPEED, now + 1)
        ctl.submit(lane, "vehicle.speed", 0, now + 1 + travel(FAR_MM - STOP_MM))
        visit.phase = "back_off"
        visit.outcome = outcome
        visit.until = now + 1 + travel(FAR_MM - STOP_MM)
        if lane == "entry":
            car.state = "refused"
        elif outcome == "abandoned":
            car.state = "parked"
            car.profile["quirk"] = "none"
            car.ready_tick = now + car.park_s * 100
        else:
            car.state = "stuck"

    def _finish(self, visit, now):
        self.visits[visit.lane] = None
        self.lane_free_at[visit.lane] = now + DISPATCH_GAP
