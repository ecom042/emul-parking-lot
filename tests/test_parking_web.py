import concurrent.futures
from pathlib import Path
import queue
import sys
import threading
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from parking_web import ParkingEngine
from scenario import runner
from scenario.assessment import FirmwareFailure, HarnessFailure, LaneMonitor, LotMonitor, assess
from test_parking_observations import cycle

RUNS = {owner: index + 1 for index, owner in enumerate(runner.OWNERS)}


class FakeTransport:
    """In-memory stand-in for the typed embedded boundary (no Renode)."""

    def __init__(self):
        self.tick = 0
        self.seq = {owner: 0 for owner in runner.OWNERS}
        self.pending = {owner: [] for owner in runner.OWNERS}
        self.requests = []
        self.on_step = lambda fake: None

    def emit(self, owner, signal, value, device="signals", category="commanded"):
        self.seq[owner] += 1
        self.pending[owner].append({
            "v": 1, "run": RUNS[owner], "seq": self.seq[owner], "time_us": self.tick * 10000,
            "tick": self.tick, "category": category, "device": device,
            "signal": signal, "value": value})

    def state(self):
        lanes = {lane: {} for lane in runner.LANES}
        for owner in runner.OWNERS:
            lane, kind = owner.split(".")
            lanes[lane][kind] = {"run": RUNS[owner], "tick": self.tick, "pending": 0,
                                 "time_us": self.tick * 10000, "position_mdeg": 0,
                                 "enable": False, "green": False}
        events, self.pending = self.pending, {owner: [] for owner in runner.OWNERS}
        return {"lanes": lanes, "events": events, "display": {}}

    def request(self, payload):
        self.requests.append(payload)
        if payload["op"] == "submit":
            return {"accepted": True}
        if payload["op"] == "step":
            self.tick += payload["ticks"]
            self.on_step(self)
        return self.state()


def spec(actions=(), expect=(), duration=100):
    lane = {"initial": {"position_mdeg": 0, "vehicle_mm": 6000}, "green": []}
    return {"duration_ticks": duration, "lanes": {"entry": dict(lane), "exit": dict(lane)},
            "actions": list(actions), "expect": list(expect)}


def controller(fake, run_spec, **options):
    return runner.RunController(fake.request, run_spec, fake.state(), seed=1, capacity=2,
                                step_ticks=100, **options)


class DomainTests(unittest.TestCase):
    def setUp(self):
        self.engine = ParkingEngine.__new__(ParkingEngine)
        self.engine.scenarios = {"normal": {}}
        self.engine.stop = threading.Event()
        self.engine.requests = queue.Queue(maxsize=1)
        self.command = {"v": 1, "run": "a" * 32, "id": "test", "op": "start"}

    def submit(self, **fields):
        return {**self.command, "op": "submit", "lane": "entry", **fields}

    def test_command_shapes_types_and_monitor_injection(self):
        for change in [
            {"v": True}, {"v": 2}, {"op": "monitor"}, {"op": []},
            {"run": 1}, {"id": "x\nquit"}, {"monitor": "quit"},
            {"op": "select", "scenario": "../normal"},
            self.submit(signal="motor.enable", value=1),
            self.submit(signal="vehicle.speed", value=True),
            self.submit(signal="vehicle.speed", value=-1999),
            self.submit(signal='fault.jam";quit', value=1),
            self.submit(signal="fault.limits", value=4),
            self.submit(lane="side", signal="fault.jam", value=1),
            self.submit(lane=["exit"], signal="fault.jam", value=1),
            self.submit(signal="payment.replay", value=1),
            {**self.command, "op": "submit", "signal": "fault.jam", "value": 1},
        ]:
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.engine.command({**self.command, **change})
        self.assertTrue(self.engine.requests.empty())

    def test_valid_commands_pass_owned_queue(self):
        def respond():
            payload, future = self.engine.requests.get(timeout=1)
            self.assertEqual((payload["lane"], payload["signal"]), ("exit", "payment.replay"))
            future.set_result({"accepted": True})
        worker = threading.Thread(target=respond)
        worker.start()
        result = self.engine.command(self.submit(lane="exit", signal="payment.replay", value=1))
        worker.join()
        self.assertTrue(result["accepted"])

    def test_full_queue_rejects_without_blocking(self):
        self.engine.requests.put(({}, concurrent.futures.Future()))
        with self.assertRaisesRegex(ValueError, "queue full"):
            self.engine.command(self.command)

    def test_stale_and_duplicate_ids_never_execute(self):
        self.engine.epoch = "b" * 32
        self.engine.ids = set()
        with self.assertRaisesRegex(ValueError, "stale_run"):
            self.engine._execute(self.command)
        self.engine.epoch = self.command["run"]
        self.engine.ids.add(self.command["id"])
        with self.assertRaisesRegex(ValueError, "duplicate"):
            self.engine._execute(self.command)

    def test_command_ids_are_bounded(self):
        self.engine.epoch = self.command["run"]
        self.engine.ids = set(range(4096))
        with self.assertRaisesRegex(ValueError, "capacity"):
            self.engine._execute(self.command)

    def test_reset_recovers_after_command_capacity(self):
        from unittest.mock import Mock
        self.engine.epoch = self.command["run"]
        self.engine.ids = set(range(4096))
        self.engine._reset = Mock()
        self.assertTrue(self.engine._execute({**self.command, "op": "reset"})["accepted"])
        self.engine._reset.assert_called_once()


class RunControllerTests(unittest.TestCase):
    def test_failed_scheduled_action_is_harness_failure(self):
        fake = FakeTransport()
        run = controller(fake, spec(actions=[{"at_tick": 1, "lane": "exit", "kind": "submit",
                                              "signal": "fault.jam", "value": 1}]))
        fake.on_step = lambda f: f.emit("exit.physical", "failed", run.command_id,
                                        "timeline", "command")
        with self.assertRaisesRegex(HarnessFailure, "scheduled scenario"):
            run.advance()
        submit = next(r for r in fake.requests if r["op"] == "submit")
        self.assertEqual(submit["owner"], "exit.physical")
        self.assertEqual(submit["command"]["run"], RUNS["exit.physical"])

    def test_failed_interactive_action_remains_visible(self):
        fake = FakeTransport()
        run = controller(fake, spec())
        run.submit("entry", "vehicle.next", 1)
        fake.on_step = lambda f: f.emit("entry.physical", "failed", 1, "timeline", "command")
        self.assertTrue(run.advance())
        self.assertEqual(run.commands[0]["status"], "failed")
        self.assertEqual(run.commands[0]["owner"], "entry.physical")

    def test_steps_stop_exactly_at_action_and_checkpoint_boundaries(self):
        fake = FakeTransport()
        run = controller(fake, spec(
            actions=[{"at_tick": 50, "lane": "entry", "kind": "submit",
                      "signal": "sensor.noise", "value": 0}],
            expect=[{"at_tick": 20, "lane": "exit", "state": {"enable": False}}]))
        ticks = []
        while not run.advance():
            ticks.append(run.tick)
        self.assertEqual(ticks, [20, 49])
        self.assertEqual(run.tick, 100)
        submit = next(r for r in fake.requests if r["op"] == "submit")
        self.assertEqual(submit["command"]["at_tick"], 50)
        self.assertEqual(run.results[0]["lane"], "exit")

    def test_events_between_steps_reach_the_oracles(self):
        fake = FakeTransport()
        run = controller(fake, spec())
        fake.emit("exit.protocol", "buzzer", 1)
        run.request({"op": "snapshot"})
        run.advance()
        self.assertEqual(run.monitors["exit"].counts["buzzer"], 1)

    def test_payment_actions_are_exit_only(self):
        run = controller(FakeTransport(), spec())
        with self.assertRaises(ValueError):
            run.submit("entry", "payment.replay", 1)
        with self.assertRaises(ValueError):
            run.configure("entry", "payment", {})

    def test_required_capacity_must_match_build(self):
        with self.assertRaisesRegex(HarnessFailure, "capacity 3"):
            controller(FakeTransport(), spec(), required_capacity=3)


def lot_event(at, lane, kind, plate):
    return (at, 0 if kind == "passage" else 1, lane, kind, {"plate": plate})


class LotTests(unittest.TestCase):
    def test_capacity_sessions_and_exit_authorization(self):
        lot = LotMonitor(2)
        lot.apply([lot_event(1, "entry", "green", "AAA1A11"),
                   lot_event(2, "entry", "passage", "AAA1A11"),
                   lot_event(3, "entry", "green", "BBB2B22"),
                   lot_event(4, "entry", "passage", "BBB2B22")])
        self.assertEqual(set(lot.sessions), {"AAA1A11", "BBB2B22"})
        with self.assertRaisesRegex(FirmwareFailure, "capacity_exceeded"):
            LotMonitor(1).apply([lot_event(1, "entry", "green", "AAA1A11"),
                                 lot_event(2, "entry", "passage", "AAA1A11"),
                                 lot_event(3, "entry", "green", "BBB2B22")])
        with self.assertRaisesRegex(FirmwareFailure, "duplicate_entry"):
            LotMonitor(4).apply([lot_event(1, "entry", "green", "AAA1A11"),
                                 lot_event(2, "entry", "passage", "AAA1A11"),
                                 lot_event(3, "entry", "green", "AAA1A11")])
        with self.assertRaisesRegex(FirmwareFailure, "exit_without_session"):
            LotMonitor(4).apply([lot_event(1, "exit", "green", "AAA1A11")])

    def test_concurrent_exit_frees_spot_in_time_order(self):
        lot = LotMonitor(1)
        lot.apply([lot_event(1, "entry", "green", "AAA1A11"),
                   lot_event(2, "entry", "passage", "AAA1A11")])
        # Same harvest: the exit passage at 5 precedes the entry green at 6,
        # even though the entry lane is processed first.
        lot.apply([lot_event(6, "entry", "green", "BBB2B22"),
                   lot_event(4, "exit", "green", "AAA1A11"),
                   lot_event(5, "exit", "passage", "AAA1A11")])
        self.assertEqual(lot.sessions, {})


def source_events(events, run=1):
    return [{**e, "v": 1, "run": run, "seq": i + 1, "tick": max(1, e["time_us"] // 10000)}
            for i, e in enumerate(events)]


class RollingMonitorTests(unittest.TestCase):
    def test_incremental_monitor_matches_whole_list_assessment(self):
        physical, protocol = cycle()
        p, q = source_events(physical), source_events(protocol)
        windows = [{"after_tick": 0, "before_tick": 100, "payment_required": True}]
        assess(p, q, {"time_us": 40}, windows, final=True)
        monitor = LaneMonitor("exit", [dict(windows[0])])
        for index in range(len(q)):
            monitor.feed(p if index == 0 else [], q[index:index + 1], 40)
        monitor.finish(True)
        self.assertEqual(monitor.cycles[0]["plate"], "ABC1D23")

    def test_green_off_discards_closed_cycle_frames(self):
        physical, protocol = cycle()
        p, q = source_events(physical), source_events(protocol)
        off = {**q[-1], "seq": len(q) + 1, "value": 0, "time_us": 50}
        monitor = LaneMonitor("exit")
        monitor.feed(p, q + [off], 60)
        self.assertEqual(monitor.protocol, [off])
        self.assertEqual([e["signal"] for e in monitor.commanded], ["direction"])
        # A later green without a fresh camera/charge cannot reuse old frames.
        again = {**q[-1], "seq": len(q) + 2, "time_us": 70}
        with self.assertRaisesRegex(FirmwareFailure, "green_authorization"):
            monitor.feed([], [again], 80)

    def test_passage_after_green_is_reported_once(self):
        physical, protocol = cycle()
        beam = [dict(time_us=40, signal="obstruction", value=1, device="beam", category="sensor"),
                dict(time_us=45, signal="obstruction", value=0, device="beam", category="sensor")]
        p, q = source_events(physical + beam), source_events(protocol)
        monitor = LaneMonitor("entry")
        events = monitor.feed(p, q, 50)
        self.assertEqual([e[3] for e in events], ["green", "passage"])
        self.assertEqual(monitor.feed([], [], 60), [])


if __name__ == "__main__":
    unittest.main()
