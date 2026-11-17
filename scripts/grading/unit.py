"""Student unit-test suites (ztest + FFF) on the lab board in Renode.

Every directory `<app>/tests/<suite>/` with a CMakeLists.txt is a Zephyr
application built for the lab board (parking_lot_u5) like the firmware. It runs headless in
Renode on the same machine description, without a monitor port, with its
console written to a file. Built for coverage (grading/gcov.py), the suite
dumps its gcov counters on that console when main() returns, after ztest's
summary. A suite passes when ztest prints "PROJECT EXECUTION SUCCESSFUL" and
no failure.
"""
from pathlib import Path
import re

from grading import gcov, workspace

RUN_SECONDS = 60  # virtual-time bound; the runner stops Renode once ztest ends
TIMEOUT_S = 300
SUCCESS = "PROJECT EXECUTION SUCCESSFUL"
FAILURE = "PROJECT EXECUTION FAILED"


def suites(app):
    tests = Path(app) / "tests"
    if not tests.is_dir():
        return []
    return [d for d in sorted(tests.iterdir()) if (d / "CMakeLists.txt").is_file()]


def _path(path):
    text = str(path)
    if any(c in text for c in ['"', ";", "\n", "\r", "\\"]):
        raise SystemExit(f"unsupported character in path {text}")
    return "@" + text.replace(" ", "\\ ")


def script(boot, console, seconds=RUN_SECONDS):
    """Monitor script: the prepared machine with its console to a file."""
    return "\n".join([
        f"include {_path(boot)}",
        f"sysbus.usart1 CreateFileBackend {_path(console)} true",
        f'emulation RunFor "{seconds}"',
        "quit",
    ]) + "\n"


def verdict(console_text):
    """'pass', 'fail' (a test failed) or 'incomplete' (no ztest summary)."""
    if FAILURE in console_text or " FAIL - " in console_text:
        return "fail"
    return "pass" if SUCCESS in console_text else "incomplete"


def prepare(module, renode, output, suite, cmake_args=()):
    """Build one suite into output/; return (job, error) where job runs it."""
    boot, error = workspace.build_firmware(module, renode, output, suite, cmake_args)
    if error:
        return None, error
    resc = output / "unit.resc"
    resc.write_text(script(boot, output / "console.log"))
    command = [str(Path(renode).resolve()), "--disable-gui", "--plain", "--hide-log",
               "-P", "-1", "-e", f"include {_path(resc)}"]
    console = output / "console.log"
    # The gcov dump follows ztest's summary in both outcomes; once it ends,
    # stopping Renode loses nothing.
    def finished():
        text = console.read_text(errors="replace") if console.is_file() else ""
        return "PROJECT EXECUTION" in text and "GCOV_COVERAGE_DUMP_END" in text
    return (command, output / "renode.log", TIMEOUT_S, finished), None


def outcome(output, code):
    console = output / "console.log"
    text = console.read_text(errors="replace") if console.is_file() else ""
    status = "timeout" if code == -9 else verdict(text)
    failed = [m.group(1) for m in re.finditer(r"^ FAIL - (\S+) in ", text, re.M)]
    return status, failed


def dumps(output):
    """The gcov dumps a suite printed (normally one)."""
    console = Path(output) / "console.log"
    return gcov.parse_dumps(console.read_text(errors="replace")) if console.is_file() else []
