"""Host checks for unit-suite discovery/verdicts and scenario statuses."""
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from grading import unit, workspace  # noqa: E402


class Suites(unittest.TestCase):
    def test_student_template_is_a_suite(self):
        self.assertEqual([s.name for s in unit.suites(ROOT / "student")], ["unit"])

    def test_only_directories_with_cmake_are_suites(self):
        with tempfile.TemporaryDirectory() as app:
            (Path(app) / "tests/a").mkdir(parents=True)
            (Path(app) / "tests/a/CMakeLists.txt").write_text("")
            (Path(app) / "tests/notes").mkdir()
            self.assertEqual([s.name for s in unit.suites(app)], ["a"])
        self.assertEqual(unit.suites("/nonexistent"), [])

    def test_verdicts(self):
        self.assertEqual(unit.verdict(" PASS - t in 0.001 seconds\nPROJECT EXECUTION SUCCESSFUL"),
                         "pass")
        self.assertEqual(unit.verdict(" FAIL - t in 0.001 seconds\nPROJECT EXECUTION FAILED"),
                         "fail")
        self.assertEqual(unit.verdict("*** Booting Zephyr ***"), "incomplete")

    def test_script_runs_machine_with_console_and_coverage(self):
        text = unit.script(Path("/b/boot.resc"), Path("/o/console.log"), Path("/o/b.txt"))
        self.assertIn("include @/b/boot.resc", text)
        self.assertIn("sysbus.usart1 CreateFileBackend @/o/console.log true", text)
        self.assertIn("sysbus.cpu0 StartBlockCoverage @/o/b.txt", text)
        self.assertTrue(text.rstrip().endswith("quit"))
        with self.assertRaises(SystemExit):
            unit.script(Path('/b/"x".resc'), Path("/o/c"), Path("/o/b"))


class Status(unittest.TestCase):
    def test_host_time_is_never_a_firmware_failure(self):
        ok = {"name": "a", "finished": True, "failure": None}
        unfinished = {"name": "a", "finished": False, "failure": None}
        failed = {"name": "a", "finished": False,
                  "failure": {"kind": "firmware_failure", "message": "checkpoint"}}
        self.assertEqual(workspace.status(0, ok), "pass")
        self.assertEqual(workspace.status(1, unfinished), "timeout")
        self.assertEqual(workspace.status(-9, None), "timeout")
        crashed = {"name": "a", "finished": False,
                   "failure": {"kind": "harness_failure", "message": "runtime disconnected"}}
        self.assertEqual(workspace.status(1, failed), "fail")
        self.assertEqual(workspace.status(1, crashed), "error")
        self.assertEqual(workspace.status(1, None), "error")

    def test_default_jobs_fit_the_host(self):
        jobs = workspace.default_jobs()
        self.assertTrue(1 <= jobs <= 4)
        self.assertEqual(workspace.default_jobs(limit=1), 1)


if __name__ == "__main__":
    unittest.main()
