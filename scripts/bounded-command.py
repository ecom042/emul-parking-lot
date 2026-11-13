"""Run a command with a host deadline and retain its output and metadata.

Requires POSIX process groups (the validated host is macOS). Only the process
group started by this script is terminated on timeout or cancellation; other
labs are untouched. Descendants that create a separate session are not covered.
"""

import argparse
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import time


def run(command, timeout, output):
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("timeout must be finite and positive")
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    timed_out = False
    proc = None
    cancellation_signal = None

    def kill_group():
        if proc is not None:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass

    def cancel(signum, _frame):
        nonlocal cancellation_signal
        if cancellation_signal is None:
            cancellation_signal = signum
            # Do not raise in Popen: retain a cancellation received before
            # assignment, then kill the group as soon as its PID is known.
            kill_group()

    previous_handlers = {
        signum: signal.signal(signum, cancel)
        for signum in (signal.SIGINT, signal.SIGTERM)
    }
    try:
        with output.open("wb") as log:
            try:
                proc = subprocess.Popen(
                    command, stdin=subprocess.DEVNULL, stdout=log,
                    stderr=subprocess.STDOUT, start_new_session=True,
                )
            except OSError as error:
                log.write(f"{error}\n".encode())
                returncode = 127
            else:
                if cancellation_signal is not None:
                    kill_group()
                try:
                    returncode = proc.wait(timeout=timeout)
                except subprocess.TimeoutExpired:
                    timed_out = True
                    kill_group()
                    proc.wait()
                    returncode = 124
                except KeyboardInterrupt:
                    # Also cover callers/tests that interrupt wait directly.
                    cancel(signal.SIGINT, None)
                    proc.wait()
                    returncode = 130
            if cancellation_signal is not None:
                returncode = 128 + cancellation_signal
        metadata = {
            "command": command,
            "cwd": str(Path.cwd()),
            "timeout_seconds": timeout,
            "elapsed_seconds": round(time.monotonic() - started, 3),
            "timed_out": timed_out,
            "cancellation_signal": cancellation_signal,
            "returncode": returncode,
        }
        output.with_suffix(output.suffix + ".json").write_text(
            json.dumps(metadata, indent=2) + "\n"
        )
        return returncode
    finally:
        for signum, handler in previous_handlers.items():
            signal.signal(signum, handler)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=float, required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command[:1] == ["--"]:
        command = command[1:]
    if not command or not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("provide a command and a finite positive timeout")
    return run(command, args.timeout, args.output)


if __name__ == "__main__":
    raise SystemExit(main())
