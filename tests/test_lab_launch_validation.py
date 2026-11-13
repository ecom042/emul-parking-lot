import importlib.util
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "pilot", Path(__file__).resolve().parents[1] / "scripts/check-student-launch.py")
pilot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pilot)


class PilotEvidenceTests(unittest.TestCase):
    def test_only_complete_checks_and_teardown_can_pass(self):
        record = {"status": "checks_passed", "returncode": 0,
                  "runtime_absent_after_close": True, "http_closed_after_close": True}
        self.assertEqual(pilot.overall_status(record), "pass")
        for field, value in (("status", "pending"), ("returncode", 1),
                             ("runtime_absent_after_close", False),
                             ("http_closed_after_close", False)):
            self.assertEqual(pilot.overall_status({**record, field: value}), "fail")

    def test_network_audit_retains_stderr_and_checks_live_process(self):
        with patch.object(pilot.os, "kill") as live, patch.object(
            pilot.subprocess, "run", return_value=subprocess.CompletedProcess(
                ["lsof"], 1, "", "")):
            result = pilot.network_audit(123)
        self.assertEqual(live.call_count, 2)
        self.assertEqual(result["stderr"], "")
        self.assertEqual(result["returncode"], 1)

    def test_network_diagnostics_or_dead_process_are_not_no_sockets(self):
        for code, error in ((1, "audit error"), (2, "")):
            with patch.object(pilot.os, "kill"), patch.object(
                pilot.subprocess, "run", return_value=subprocess.CompletedProcess(
                    ["lsof"], code, "", error)):
                with self.assertRaises(RuntimeError):
                    pilot.network_audit(123)
        with patch.object(pilot.os, "kill", side_effect=ProcessLookupError):
            with self.assertRaises(ProcessLookupError):
                pilot.network_audit(123)
