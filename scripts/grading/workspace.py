"""Shared steps for grading tools: environment, firmware build, scenario runs.

The tools build a copy of the student application the same way `lab.py` does,
run real scenarios on official Renode through `assess.py`, and keep every log
under one new output directory beneath `build/`.
"""
import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
PUBLIC_SCENARIOS = ROOT / "tests/scenarios"
BUILD_TIMEOUT_S = 300
PREPARE_TIMEOUT_S = 60


def _load(name, filename):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


lab = _load("lab", "lab.py")
bounded = _load("bounded", "bounded-command.py")


def new_output(path):
    """A new directory beneath this worktree's build/, as lab.py requires."""
    output = Path(path).resolve()
    if not output.is_relative_to(ROOT / "build") or output == ROOT / "build":
        raise SystemExit("--output must be a new directory beneath this worktree's build/")
    try:
        output.mkdir(parents=True, exist_ok=False)
    except FileExistsError:
        raise SystemExit("output exists; choose a new directory (nothing was removed)")
    return output


def select_environment(module):
    """Same process environment lab.py selects, for this process and children."""
    selected = lab.environment(Path(module).resolve())
    os.chdir(ROOT)
    os.environ.clear()
    os.environ.update(selected)


def scenario_names(scenario_dirs=()):
    """Public fixtures first, then any trusted extra directories, by name."""
    names = []
    for directory in [PUBLIC_SCENARIOS, *scenario_dirs]:
        names.extend(sorted(p.stem for p in Path(directory).glob("*.json")))
    if len(names) != len(set(names)):
        raise SystemExit("scenario names must be unique across --scenarios-dir")
    return names


def build_firmware(module, renode, output, app, cmake_args=(), capacity=None):
    """Build and prepare `app` into output/firmware; return boot.resc."""
    build, prepare, _ = lab.commands(Path(module).resolve(), Path(renode).resolve(), output,
                                     headless="all", capacity=capacity, app=Path(app).resolve(),
                                     cmake_args=list(cmake_args))
    for command, timeout, name in ((build, BUILD_TIMEOUT_S, "build"),
                                   (prepare, PREPARE_TIMEOUT_S, "prepare")):
        code = bounded.run(command, timeout, output / f"{name}.log")
        if code:
            return None, f"{name} failed ({code}); inspect {output / (name + '.log')}"
    return output / "firmware/boot.resc", None


def assess_command(module, renode, boot, output, names, scenario_dirs=(), trace_dir=None):
    """assess.py runs `names` in order in one Renode process and stops at the
    first failure; its records (name/finished/failure) land in results.json."""
    command = [sys.executable, str(SCRIPTS / "assess.py"), "--module", str(module),
               "--renode", str(renode), "--machine-script", str(boot),
               "--output", str(output)]
    for directory in scenario_dirs:
        command += ["--scenarios-dir", str(directory)]
    for name in names:
        command += ["--scenario", name]
    if trace_dir is not None:
        command += ["--trace-dir", str(trace_dir)]
    return command


def records(output):
    results = Path(output) / "results.json"
    return json.loads(results.read_text())["results"] if results.is_file() else []


def run_parallel(jobs, workers):
    """Run (command, log path, timeout s) jobs, at most `workers` at a time.

    Each job gets its own process group, killed on timeout or interruption.
    Returns exit codes in job order (-9 after a timeout). Virtual-time
    deadlines are per Renode process, so parallel runs cannot change verdicts.
    """
    codes = [None] * len(jobs)
    running = {}
    pending = list(enumerate(jobs))
    try:
        while pending or running:
            while pending and len(running) < max(1, workers):
                index, (command, log, timeout) = pending.pop(0)
                Path(log).parent.mkdir(parents=True, exist_ok=True)
                stream = open(log, "wb")
                proc = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=stream,
                                        stderr=subprocess.STDOUT, start_new_session=True)
                running[index] = (proc, stream, time.monotonic() + timeout)
            for index, (proc, stream, deadline) in list(running.items()):
                code = proc.poll()
                if code is None and time.monotonic() > deadline:
                    _kill(proc)
                    code = -9
                if code is not None:
                    stream.close()
                    codes[index] = code
                    del running[index]
            time.sleep(0.2)
    finally:
        for proc, stream, _ in running.values():
            _kill(proc)
            stream.close()
    return codes


_children = set()
_children_lock = threading.Lock()


def run_one(command, log, timeout):
    """Run one command to `log` in its own process group; safe from threads.

    Returns its exit code, or -9 after killing it at `timeout` seconds.
    """
    Path(log).parent.mkdir(parents=True, exist_ok=True)
    with open(log, "wb") as stream:
        proc = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=stream,
                                stderr=subprocess.STDOUT, start_new_session=True)
        with _children_lock:
            _children.add(proc)
        try:
            return proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            _kill(proc)
            return -9
        finally:
            with _children_lock:
                _children.discard(proc)


def kill_children():
    """Stop every command still running from run_one (e.g. after Ctrl-C)."""
    with _children_lock:
        running = list(_children)
    for proc in running:
        _kill(proc)


def _kill(proc):
    try:
        os.killpg(proc.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    proc.wait()


def source_hashes(app):
    """sha256 of every file an application build reads, keyed by relative path."""
    return lab.source_hashes(Path(app), ["CMakeLists.txt", "Kconfig", "prj.conf",
                                         "app.overlay", "boards", "src"])


def passed(record):
    return bool(record.get("finished")) and not record.get("failure")
