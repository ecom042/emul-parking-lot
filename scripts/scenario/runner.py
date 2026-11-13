"""One finite virtual-time stepping core for browser, headless and Robot runs.

The caller supplies `request(payload) -> result`, the typed private boundary
implemented by `parking_embedded.py` (stdio) or the Robot transport. Every
state-returning result has the shape

    {"lanes": {"entry": {"physical": {...}, "protocol": {...}}, "exit": {...}},
     "display": {...}, "pixels"?: "...", "events": {"entry.physical": [...], ...}}

The controller schedules typed actions, optional driver decisions and
checkpoints at exact tick boundaries, so a run steps identically whichever
transport and pacing are used.
"""

import collections

from scenario import parser
from scenario.assessment import (EvidenceLog, HarnessFailure, LaneMonitor, LotMonitor,
                                 Stream, checkpoint)

LANES = parser.LANES
KINDS = ("physical", "protocol")
OWNERS = tuple(f"{lane}.{kind}" for lane in LANES for kind in KINDS)
# Interactive car-queue runs: one virtual hour, the equipment scheduling horizon.
OPEN_RUN_TICKS = 360000


def owner_of(lane, signal):
    return f"{lane}.{parser.owner(signal)}"


def source(state, owner):
    lane, kind = owner.split(".")
    return state["lanes"][lane][kind]


def open_run():
    """Unscheduled run spec: any number of authorized greens per lane."""
    lane = {"initial": {"position_mdeg": 0, "vehicle_mm": 6000}}
    return {"duration_ticks": OPEN_RUN_TICKS, "actions": [], "expect": [],
            "lanes": {name: dict(lane) for name in LANES}, "open": True}


def reset(request, previous):
    """Coherently reset the machine and prove old-run commands now reject."""
    state = request({"op": "reset"})
    for owner in OWNERS:
        before, after = source(previous, owner), source(state, owner)
        if after["run"] == before["run"]:
            raise HarnessFailure("reset retained source run identity")
        if after["time_us"] < before["time_us"]:
            raise HarnessFailure("reset moved source time backwards")
        lane, kind = owner.split(".")
        signal = "sensor.noise" if kind == "physical" else (
            "payment.replay" if lane == "exit" else None)
        if signal is None:
            continue
        ack = request({"op": "submit", "owner": owner, "command": {
            "v": 1, "run": before["run"], "id": 2147483647, "at_tick": 1,
            "signal": signal, "value": 0 if kind == "physical" else 1}})
        if ack.get("accepted") is not False or ack.get("error") != "stale_run":
            raise HarnessFailure("old source run command did not reject")
    return state


class RunController:
    """Own one reset-scoped run: configuration, stepping, oracles and evidence."""

    def __init__(self, request, spec, state, *, seed, capacity, motor_ms=2500,
                 step_ticks=5, evidence=None, driver=None, required_capacity=None):
        parser.integer(step_ticks, 1, 100)
        if required_capacity is not None and required_capacity != capacity:
            raise HarnessFailure(f"scenario requires capacity {required_capacity}; "
                                 f"this build has {capacity}")
        self._request = request
        self.spec = spec
        self.scheduled = not spec.get("open")
        self.step_ticks = step_ticks
        self.driver = driver
        self.commands = collections.deque(maxlen=128)
        self.results = collections.deque(maxlen=128)
        self.processed = set()
        self.checked = set()
        self.scheduled_ids = set()
        self.command_id = 0
        self.finished = False
        self.log = EvidenceLog(evidence) if evidence else None
        self.state = state
        for owner in OWNERS:
            current = source(state, owner)
            if current["tick"] != 0 or current["pending"]:
                raise HarnessFailure("run configuration requires empty paused Tick 0")
        self.streams = {}
        for owner in OWNERS:
            current = source(state, owner)
            sink = (lambda event, name=owner: self.log.write(name, event)) if self.log else None
            self.streams[owner] = Stream(current["run"], current["time_us"], sink)
        self.monitors = {lane: LaneMonitor(lane, self._windows(lane), motor_ms) for lane in LANES}
        self.lot = LotMonitor(capacity)
        self.capacity = capacity
        self.new = {owner: [] for owner in OWNERS}
        self._update(state)
        for lane in LANES:
            config = spec["lanes"][lane]
            self.request({"op": "initial", "lane": lane, **config["initial"], "seed": seed})
            for kind in ("camera", "payment"):
                if kind in config:
                    self.configure(lane, kind, config[kind])
        if driver:
            driver.attach(self)

    def _windows(self, lane):
        if not self.scheduled:
            return None
        return [{**w, "payment_required": lane == "exit"}
                for w in self.spec["lanes"][lane]["green"]]

    @property
    def tick(self):
        return source(self.state, "entry.physical")["tick"]

    def lane_state(self, lane):
        return self.state["lanes"][lane]

    def request(self, payload):
        return self._update(self._request(payload))

    def _update(self, result):
        """Harvest a state-returning result; return it unchanged."""
        if "lanes" not in result:
            return result
        if "pixels" not in result and "pixels" in self.state:
            result["pixels"] = self.state["pixels"]
        self.state = result
        # Accumulate until the next step consumes them: configuration and
        # submission results between steps also carry source events.
        for owner in OWNERS:
            if source(result, owner)["run"] != self.streams[owner].run:
                raise HarnessFailure("unexpected source run change")
            self.new[owner] += self.streams[owner].collect(result["events"][owner])
        return result

    def configure(self, lane, kind, config):
        if kind == "payment" and lane != "exit":
            raise ValueError("payment configuration is exit-lane only")
        self.request({"op": "configure", "lane": lane, "kind": kind, "config": config})

    def submit(self, lane, signal, value, at_tick=None, *, scheduled=False):
        """Schedule a typed equipment action; default is the next source tick."""
        parser.lane_signal(lane, signal)
        owner = owner_of(lane, signal)
        self.command_id += 1
        if scheduled:
            self.scheduled_ids.add((owner, self.command_id))
        command = {"v": 1, "run": source(self.state, owner)["run"], "id": self.command_id,
                   "at_tick": self.tick + 1 if at_tick is None else at_tick,
                   "signal": signal, "value": value}
        ack = self.request({"op": "submit", "owner": owner, "command": command})
        if not ack.get("accepted"):
            raise RuntimeError(f"equipment rejected command: {ack}")
        return ack

    def replay(self, charge, delay_ticks):
        uuids = self.monitors["exit"].charges
        if charge > len(uuids):
            raise RuntimeError("replay references unobserved charge")
        self.request({"op": "replay", "uuid": uuids[charge - 1], "delay_ticks": delay_ticks})

    def _scheduled(self):
        for index, action in enumerate(self.spec["actions"]):
            if index in self.processed or action["at_tick"] != self.tick + 1:
                continue
            self.processed.add(index)
            lane = action["lane"]
            if action["kind"] == "submit":
                self.submit(lane, action["signal"], action["value"], action["at_tick"],
                            scheduled=True)
            elif action["kind"] in ("camera", "payment"):
                self.configure(lane, action["kind"], action["config"])
            else:
                self.replay(action["charge"], action["delay_ticks"])

    def boundaries(self):
        tick = self.tick
        result = [self.spec["duration_ticks"], tick + self.step_ticks]
        result += [a["at_tick"] - 1 for i, a in enumerate(self.spec["actions"])
                   if i not in self.processed and a["at_tick"] - 1 > tick]
        result += [e["at_tick"] for i, e in enumerate(self.spec["expect"])
                   if i not in self.checked and e["at_tick"] > tick]
        if self.driver:
            result += [t for t in self.driver.wake_ticks() if t > tick]
        return result

    def advance(self):
        """Run one finite step to the next boundary; return True when the run ends."""
        if self.finished:
            return True
        self._scheduled()
        if self.driver:
            self.driver.before_step(self)
        target = min(self.boundaries())
        self.request({"op": "step", "ticks": target - self.tick})
        if any(source(self.state, o)["tick"] != target for o in OWNERS):
            raise HarnessFailure("finite step did not reach its exact tick boundary")
        new, self.new = self.new, {owner: [] for owner in OWNERS}
        for owner in OWNERS:
            for event in new[owner]:
                if event["category"] == "command" and event["signal"] == "failed":
                    self.commands.append({"status": "failed", "owner": owner, "event": event})
                    if (owner, event["value"]) in self.scheduled_ids:
                        raise HarnessFailure("scheduled scenario equipment command failed")
        final = target == self.spec["duration_ticks"]
        lot_events = []
        for lane in LANES:
            physical = self.lane_state(lane)["physical"]
            lot_events += self.monitors[lane].feed(
                new[f"{lane}.physical"], new[f"{lane}.protocol"], physical["time_us"])
        self.lot.apply(lot_events)
        for lane in LANES:
            self.monitors[lane].finish(final)
        for index, expectation in enumerate(self.spec["expect"]):
            if index not in self.checked and expectation["at_tick"] == target:
                lane = expectation["lane"]
                current = self.lane_state(lane)
                checkpoint(expectation["state"], current["physical"], current["protocol"],
                           counts=self.monitors[lane].counts)
                self.checked.add(index)
                self.results.append({"status": "pass", "tick": target, "lane": lane,
                                     "expected": expectation["state"]})
        if self.driver:
            self.driver.after_step(self, new, lot_events)
        self.finished = final
        return final

    def recent_events(self, limit=128):
        return {owner: stream.events[-limit:] for owner, stream in self.streams.items()}

    def close(self):
        if self.log:
            self.log.close()
