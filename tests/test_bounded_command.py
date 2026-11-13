"""Host-only regression tests; no Renode or downloaded dependencies required."""

import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "bounded-command.py"
SPEC = importlib.util.spec_from_file_location("bounded_command", SCRIPT)
bounded = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bounded)


class BoundedCommandTests(unittest.TestCase):
    def test_invalid_deadlines_do_not_launch_commands(self):
        for timeout in (float("nan"), float("inf"), -float("inf"), 0, -1):
            with self.subTest(timeout=timeout), mock.patch.object(
                bounded.subprocess, "Popen"
            ) as launch:
                with self.assertRaises(ValueError):
                    bounded.run(["unused"], timeout, "unused.log")
                launch.assert_not_called()

    def test_cli_rejects_invalid_deadlines(self):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / "launched"
            for timeout in ("nan", "inf", "-inf", "0", "-1"):
                with self.subTest(timeout=timeout):
                    result = subprocess.run(
                        [sys.executable, str(SCRIPT), f"--timeout={timeout}",
                         "--output", str(Path(directory) / "unused.log"), "--",
                         sys.executable, "-c",
                         f"from pathlib import Path; Path({str(marker)!r}).touch()"],
                        capture_output=True, timeout=10,
                    )
                    self.assertEqual(result.returncode, 2)
                    self.assertIn(b"finite positive timeout", result.stderr)
                    self.assertFalse(marker.exists())

    def test_keyboard_interrupt_cleans_group_and_records_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "interrupt.log"
            before = signal.getsignal(signal.SIGINT)
            with mock.patch.object(bounded.subprocess, "Popen") as launch:
                proc = launch.return_value
                proc.pid = 12345
                proc.wait.side_effect = [KeyboardInterrupt, -signal.SIGKILL]
                with mock.patch.object(bounded.os, "killpg") as kill:
                    self.assertEqual(bounded.run(["unused"], 10, output), 130)
                    kill.assert_called_once_with(12345, signal.SIGKILL)
                self.assertEqual(proc.wait.call_count, 2)
            metadata = json.loads(output.with_suffix(".log.json").read_text())
            self.assertEqual(metadata["cancellation_signal"], signal.SIGINT)
            self.assertEqual(metadata["returncode"], 130)
            self.assertFalse(metadata["timed_out"])
            self.assertEqual(signal.getsignal(signal.SIGINT), before)

    def test_output_and_exit_status_are_retained(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "check.log"
            command = [
                sys.executable, "-c",
                "import sys; print('diagnostic', flush=True); sys.exit(7)",
            ]
            self.assertEqual(bounded.run(command, 10, output), 7)
            self.assertIn("diagnostic", output.read_text())
            metadata = json.loads(output.with_suffix(".log.json").read_text())
            self.assertEqual(metadata["command"], command)
            self.assertEqual(metadata["returncode"], 7)
            self.assertFalse(metadata["timed_out"])

    def test_missing_executable_is_recorded(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "missing.log"
            command = [str(Path(directory) / "does-not-exist")]
            self.assertEqual(bounded.run(command, 10, output), 127)
            self.assertIn("does-not-exist", output.read_text())
            metadata = json.loads(output.with_suffix(".log.json").read_text())
            self.assertEqual(metadata["returncode"], 127)
            self.assertFalse(metadata["timed_out"])

    def test_deadline_retains_partial_output(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "timeout.log"
            command = [
                sys.executable, "-c",
                "import time; print('started', flush=True); time.sleep(60)",
            ]
            self.assertEqual(bounded.run(command, 1, output), 124)
            self.assertIn("started", output.read_text())
            metadata = json.loads(output.with_suffix(".log.json").read_text())
            self.assertTrue(metadata["timed_out"])
            self.assertLess(metadata["elapsed_seconds"], 10)

    @unittest.skipUnless(os.name == "posix", "requires POSIX process groups")
    def test_signals_and_timeout_clean_only_owned_process_group(self):
        # Readiness records both PIDs only after the grandchild has started.
        # A zombie is no longer running; only its adopting process can reap it.
        def running(pid):
            result = subprocess.run(
                ["ps", "-p", str(pid), "-o", "stat="],
                capture_output=True, text=True, timeout=5,
            )
            return result.returncode == 0 and not result.stdout.strip().startswith("Z")

        for signum in (signal.SIGINT, signal.SIGTERM, None):
            with self.subTest(signal=signum), tempfile.TemporaryDirectory() as directory:
                ready = Path(directory) / "ready.json"
                output = Path(directory) / "group.log"
                child_code = (
                    "import json, os, subprocess, sys, time\n"
                    "from pathlib import Path\n"
                    "grandchild = subprocess.Popen([sys.executable, '-c', "
                    "'import time; time.sleep(60)'])\n"
                    "print('ready for cancellation', flush=True)\n"
                    f"Path({str(ready)!r}).write_text(json.dumps("
                    "{'child': os.getpid(), 'grandchild': grandchild.pid}))\n"
                    "time.sleep(60)\n"
                )
                sentinel = subprocess.Popen(
                    [sys.executable, "-c", "import time; time.sleep(60)"],
                    start_new_session=True,
                )
                wrapper = None
                pids = None
                try:
                    wrapper = subprocess.Popen(
                        [sys.executable, str(SCRIPT), "--timeout", "5",
                         "--output", str(output), "--", sys.executable,
                         "-c", child_code],
                        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                    )
                    deadline = time.monotonic() + 4
                    while time.monotonic() < deadline:
                        try:
                            pids = json.loads(ready.read_text())
                            break
                        except (FileNotFoundError, json.JSONDecodeError):
                            time.sleep(0.01)
                    self.assertIsNotNone(pids, "child readiness handshake missing")
                    self.assertTrue(running(pids["child"]))
                    self.assertTrue(running(pids["grandchild"]))
                    if signum is not None:
                        wrapper.send_signal(signum)
                    _, stderr = wrapper.communicate(timeout=10)
                    expected = 124 if signum is None else 128 + signum
                    self.assertEqual(wrapper.returncode, expected, stderr.decode())
                    metadata = json.loads(output.with_suffix(".log.json").read_text())
                    self.assertEqual(metadata["returncode"], expected)
                    self.assertEqual(metadata["cancellation_signal"], signum)
                    self.assertEqual(metadata["timed_out"], signum is None)
                    self.assertIn("ready for cancellation", output.read_text())
                    self.assertFalse(running(pids["child"]))
                    self.assertFalse(running(pids["grandchild"]))
                    self.assertIsNone(sentinel.poll(), "unrelated group was affected")
                finally:
                    # Even a failed assertion must not leave test-owned work.
                    if pids is None and ready.exists():
                        pids = json.loads(ready.read_text())
                    if pids is not None:
                        try:
                            os.killpg(pids["child"], signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                    if wrapper is not None:
                        if wrapper.poll() is None:
                            wrapper.terminate()
                        try:
                            wrapper.communicate(timeout=5)
                        except subprocess.TimeoutExpired:
                            wrapper.kill()
                            wrapper.communicate(timeout=5)
                    sentinel.kill()
                    sentinel.wait(timeout=5)


if __name__ == "__main__":
    unittest.main()
