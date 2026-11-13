"""Car-queue driver decisions against a controllable equipment stub (no Renode)."""
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from scenario import driver as d
from scenario.parser import ScenarioError


class Monitor:
    def __init__(self):
        self.counts = {"green": 0, "buzzer": 0}
        self.charges = []


class Ctl:
    """Minimal RunController surface the driver uses."""

    def __init__(self):
        self.tick = 0
        self.monitors = {lane: Monitor() for lane in ("entry", "exit")}
        self.lanes = {lane: {"physical": {"position_mdeg": 0, "enable": False,
                                          "direction": False},
                             "protocol": {"green": False}} for lane in ("entry", "exit")}
        self.submits = []
        self.configs = []
        self.visited = set()
        # The lot oracle's observed sessions (plate -> entry passage time).
        self.lot = SimpleNamespace(sessions={})

    def lane_state(self, lane):
        return self.lanes[lane]

    def submit(self, lane, signal, value, at_tick=None):
        self.submits.append((lane, signal, value, self.tick + 1 if at_tick is None else at_tick))

    def configure(self, lane, kind, config):
        self.configs.append((lane, kind, config))

    def run_until(self, driver, tick):
        """Visit the current tick, then the driver's wake ticks, as the runner does."""
        while True:
            if self.tick not in self.visited:
                self.visited.add(self.tick)
                driver.before_step(self)
                driver.after_step(self, {}, [])
            if self.tick >= tick:
                return
            wakes = [t for t in driver.wake_ticks() if t > self.tick] or [tick]
            self.tick = min(min(wakes), tick)


def opened(ctl, lane):
    ctl.lanes[lane]["protocol"]["green"] = True
    ctl.lanes[lane]["physical"]["position_mdeg"] = 90000


def closed(ctl, lane):
    ctl.lanes[lane]["protocol"]["green"] = False
    ctl.lanes[lane]["physical"].update(position_mdeg=0, enable=False)


def setup(*profiles, capacity=4):
    driver = d.Driver(seed=7, capacity=capacity, cars=list(profiles))
    ctl = Ctl()
    driver.attach(ctl)
    return driver, ctl


class ProfileTests(unittest.TestCase):
    def test_profiles_are_enums_only(self):
        for bad in ({"payment": "free"}, {"quirk": "monitor quit"}, {"park_s": 0},
                    {"park_s": True}, {"plate": "abc1d23"}, {"speed": 1}, []):
            with self.subTest(bad=bad), self.assertRaises(ScenarioError):
                d.validate_profile(bad)
        d.validate_profile({"plate": "ABC1D23", "park_s": 5, **d.DEFAULT_PROFILE})

    def test_queue_edits_respect_car_lifecycle(self):
        driver, ctl = setup({}, {})
        first, second = driver.cars
        with self.assertRaises(ScenarioError):
            driver.add({"plate": driver.cars[first].plate})
        driver.cars[first].state = "parked"
        with self.assertRaises(ScenarioError):
            driver.update(first, {"entry_capture": "failed"})
        driver.update(first, {"payment": "cancelled"})
        with self.assertRaises(ScenarioError):
            driver.remove(first)
        with self.assertRaises(ScenarioError):
            driver.retry(second, "paid")
        driver.remove(second)
        self.assertEqual(list(driver.cars), [first])

    def test_plates_are_valid_and_unique(self):
        driver, _ = setup(*([{}] * 40))
        plates = [c.plate for c in driver.cars.values()]
        self.assertEqual(len(set(plates)), 40)
        for plate in plates:
            d.validate_profile({"plate": plate})


class LaneTests(unittest.TestCase):
    def test_entry_approach_configures_camera_and_stops_at_identification_point(self):
        driver, ctl = setup({"entry_capture": "retry_recovers"})
        ctl.run_until(driver, 10)
        lane, kind, config = ctl.configs[0]
        self.assertEqual((lane, kind, config["confidences"]), ("entry", "camera", [60, 93]))
        speeds = [(s[2], s[3]) for s in ctl.submits if s[1] == "vehicle.speed"]
        self.assertEqual(speeds, [(-2000, 1), (0, 1 + 260)])

    def test_valid_entry_passes_parks_and_exits(self):
        driver, ctl = setup({"park_s": 2})
        car = next(iter(driver.cars.values()))
        ctl.run_until(driver, 300)
        opened(ctl, "entry")
        ctl.run_until(driver, 310)
        self.assertEqual(driver.verdicts[-1]["status"], "pass")
        self.assertEqual(driver.verdicts[-1]["observed"], "entered")
        beam = [(s[2], s[3] - 310) for s in ctl.submits if s[1] == "sensor.obstruction"]
        self.assertEqual(beam, [(1, 31), (0, 131)])
        ctl.run_until(driver, 470)
        self.assertEqual((car.state, car.spot), ("parked", 0))
        closed(ctl, "entry")
        ctl.run_until(driver, 480)
        self.assertIn(("entry", "vehicle.next", 1, 481), ctl.submits)
        ctl.run_until(driver, 700)
        self.assertEqual(car.state, "exiting")
        payment = [c for c in ctl.configs if c[1] == "payment"][-1]
        self.assertEqual(payment[2]["result"], "PAID")
        ctl.run_until(driver, 1000)
        opened(ctl, "exit")
        ctl.run_until(driver, 1010)
        ctl.run_until(driver, 1200)
        self.assertEqual(car.state, "gone")
        self.assertEqual([v["observed"] for v in driver.verdicts], ["entered", "exited"])

    def test_failed_capture_expects_assistance_not_a_timeout(self):
        driver, ctl = setup({"entry_capture": "failed"}, {"entry_capture": "failed"})
        first, second = driver.cars.values()
        ctl.run_until(driver, 400)
        ctl.monitors["entry"].counts["buzzer"] = 1
        ctl.run_until(driver, 410)
        self.assertEqual((driver.verdicts[-1]["status"], first.state), ("pass", "refused"))
        back = [s for s in ctl.submits if s[1] == "vehicle.speed" and s[2] == 2000]
        self.assertTrue(back)
        # Second car times out without assistance: that is a firmware failure.
        ctl.run_until(driver, 2500)
        self.assertEqual(driver.verdicts[-1]["observed"], "ignored")
        self.assertEqual(driver.verdicts[-1]["status"], "fail")
        self.assertEqual(second.state, "refused")

    def test_full_lot_refusal_is_expected(self):
        driver, ctl = setup({}, capacity=1)
        driver.add({})
        parked = next(iter(driver.cars.values()))
        parked.state, parked.ready_tick = "parked", 10**9
        ctl.lot.sessions[parked.plate] = 0
        ctl.run_until(driver, 400)
        ctl.monitors["entry"].counts["buzzer"] = 1
        ctl.run_until(driver, 410)
        self.assertEqual(driver.verdicts[-1]["expected"], ["entered", "refused"])
        self.assertEqual(driver.verdicts[-1]["status"], "pass")

    def test_spot_freed_by_exit_beam_allows_waiting_entry(self):
        # The lot oracle frees the spot when the exit beam clears, before the
        # driver finishes that car's manoeuvre. An entry opening then is right.
        driver, ctl = setup({}, capacity=1)
        driver.add({})
        leaving = next(iter(driver.cars.values()))
        leaving.state, leaving.spot = "exiting", 0
        ctl.lot.sessions[leaving.plate] = 0
        ctl.run_until(driver, 300)
        del ctl.lot.sessions[leaving.plate]
        opened(ctl, "entry")
        ctl.run_until(driver, 310)
        self.assertEqual(leaving.state, "exiting")
        self.assertEqual(driver.verdicts[-1]["observed"], "entered")
        self.assertEqual(driver.verdicts[-1]["status"], "pass")

    def test_entry_counted_as_parked_by_the_oracle_fills_the_lot(self):
        # Conversely an entry's session opens at its beam clear: the next car
        # must already expect refusal even though the driver still says entering.
        driver, ctl = setup({}, capacity=1)
        ctl.lot.sessions["ZZZ9Z99"] = 0
        ctl.run_until(driver, 300)
        opened(ctl, "entry")
        ctl.run_until(driver, 310)
        self.assertEqual(driver.verdicts[-1]["expected"], ["refused"])
        self.assertEqual(driver.verdicts[-1]["status"], "fail")

    def test_totals_keep_failures_beyond_the_recent_window(self):
        driver, ctl = setup({})
        visit = d.Visit(next(iter(driver.cars.values())), "entry", 0)
        driver._verdict(visit, "refused", {"entered"}, 0)
        for tick in range(300):
            driver._verdict(visit, "entered", {"entered"}, tick)
        self.assertEqual(len(driver.verdicts), 256)
        self.assertTrue(all(v["status"] == "pass" for v in driver.verdicts))
        self.assertEqual(driver.view()["totals"], {"pass": 300, "fail": 1})
        self.assertEqual([v["status"] for v in driver.view()["failures"]], ["fail"])

    def test_refused_exit_is_stuck_until_retry(self):
        driver, ctl = setup({"exit_capture": "misread"})
        car = next(iter(driver.cars.values()))
        car.state, car.ready_tick, car.spot = "exit_queued", 0, 0
        ctl.run_until(driver, 10)
        camera = [c for c in ctl.configs if c[0] == "exit" and c[1] == "camera"][-1]
        self.assertNotEqual(camera[2]["plate"], car.plate)
        ctl.run_until(driver, 400)
        ctl.monitors["exit"].counts["buzzer"] = 1
        ctl.run_until(driver, 410)
        self.assertEqual((car.state, driver.verdicts[-1]["status"]), ("stuck", "pass"))
        ctl.run_until(driver, 800)
        self.assertEqual(car.state, "stuck")
        driver.retry(car.id, "paid")
        self.assertEqual(car.profile["exit_capture"], "normal")
        ctl.run_until(driver, 820)
        camera = [c for c in ctl.configs if c[0] == "exit" and c[1] == "camera"][-1]
        self.assertEqual(camera[2]["plate"], car.plate)

    def test_driving_away_during_payment(self):
        driver, ctl = setup({"quirk": "leave_during_payment"})
        car = next(iter(driver.cars.values()))
        car.state, car.ready_tick = "exit_queued", 0
        ctl.run_until(driver, 300)
        ctl.monitors["exit"].charges.append("uuid")
        ctl.run_until(driver, 310)
        self.assertEqual(driver.verdicts[-1]["observed"], "abandoned")
        self.assertEqual(driver.verdicts[-1]["status"], "pass")
        opened(ctl, "exit")
        ctl.run_until(driver, 320)
        self.assertEqual(driver.verdicts[-1]["observed"], "opened_after_abandon")
        self.assertEqual(driver.verdicts[-1]["status"], "fail")
        self.assertEqual(car.state, "parked")

    def test_decisions_only_on_the_wake_grid(self):
        driver, ctl = setup({})
        ctl.tick = 5
        driver.before_step(ctl)
        self.assertEqual(ctl.submits, [])
        self.assertTrue(all(t % d.POLL == 0 or t > 5 for t in driver.wake_ticks()))

    def test_restart_requeues_every_configured_car(self):
        driver, _ = setup({"payment": "cancelled"}, {})
        first = next(iter(driver.cars.values()))
        first.state = "stuck"
        again = driver.restart()
        self.assertEqual([c.state for c in again.cars.values()], ["queued", "queued"])
        self.assertEqual(next(iter(again.cars.values())).plate, first.plate)
        self.assertEqual(next(iter(again.cars.values())).profile["payment"], "cancelled")


if __name__ == "__main__":
    unittest.main()
