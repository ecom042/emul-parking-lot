import concurrent.futures
from pathlib import Path
import queue
import sys
import threading
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from parking_web import ParkingEngine


class DomainTests(unittest.TestCase):
    def setUp(self):
        self.engine = ParkingEngine.__new__(ParkingEngine)
        self.engine.scenarios = {"normal": {}}
        self.engine.stop = threading.Event()
        self.engine.requests = queue.Queue(maxsize=1)
        self.command = {"v": 1, "run": "a" * 32, "id": "test", "op": "start"}

    def test_command_shapes_types_and_monitor_injection(self):
        for change in [
            {"v": True}, {"v": 2}, {"op": "monitor"}, {"op": []},
            {"run": 1}, {"id": "x\nquit"}, {"monitor": "quit"},
            {"op": "select", "scenario": "../normal"},
            {"op": "submit", "signal": "motor.enable", "value": 1},
            {"op": "submit", "signal": "vehicle.speed", "value": True},
            {"op": "submit", "signal": "vehicle.speed", "value": -1999},
            {"op": "submit", "signal": 'fault.jam";quit', "value": 1},
            {"op": "submit", "signal": "fault.limits", "value": 4},
        ]:
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.engine.command({**self.command, **change})
        self.assertTrue(self.engine.requests.empty())

    def test_valid_commands_pass_owned_queue(self):
        def respond():
            payload, future = self.engine.requests.get(timeout=1)
            self.assertEqual(payload["signal"], "vehicle.speed")
            future.set_result({"accepted": True})
        worker = threading.Thread(target=respond)
        worker.start()
        result = self.engine.command({**self.command, "op": "submit",
                                      "signal": "vehicle.speed", "value": -2000})
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

    def deferred_failure(self, scheduled):
        from collections import deque
        from types import SimpleNamespace
        from unittest.mock import Mock, patch
        self.engine.state = {"physical": {"tick": 0}, "protocol": {"tick": 0}}
        self.engine.step_ticks = 100
        self.engine.run_spec = {"duration_ticks": 100, "actions": [], "expect": [], "green": []}
        self.engine.processed = self.engine.checked = set()
        self.engine.scheduled_ids = {7} if scheduled else set()
        self.engine.commands = deque()
        self.engine.streams = {o: SimpleNamespace(events=[]) for o in ("physical", "protocol")}
        self.engine._scheduled = Mock()
        self.engine._retain_run = Mock()
        self.engine.scenario = None
        def step(_):
            self.engine.state = {
                "physical": {"tick": 100}, "protocol": {"tick": 100},
                "events": {"physical": [{"category": "command", "signal": "failed", "value": 7}],
                           "protocol": []}}
        self.engine._rpc = step
        with patch("parking_web.assess"):
            self.engine._advance()

    def test_failed_scheduled_action_is_harness_failure(self):
        from scenario.assessment import HarnessFailure
        with self.assertRaisesRegex(HarnessFailure, "scheduled scenario"):
            self.deferred_failure(scheduled=True)

    def test_failed_interactive_action_remains_visible(self):
        self.deferred_failure(scheduled=False)
        self.assertEqual(self.engine.commands[0]["status"], "failed")
        self.assertTrue(self.engine.finished)


if __name__ == "__main__":
    unittest.main()
