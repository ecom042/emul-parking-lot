"""Line and branch coverage of student sources with gcov.

Coverage builds (COVERAGE_CMAKE) instrument only the application with
`-fprofile-arcs -ftest-coverage` (scripts/grading/gcov_module moves Zephyr's
global flags to the `app` target, so the kernel keeps its timing) and link
Zephyr's gcov runtime. A scenario run asks the firmware for its counters at
its end and before every machine reset (scripts/parking_embedded.py pulses an
interrupt the module connects to gcov_coverage_dump()); a ztest suite dumps
them when main() returns. Each dump is the console text between
GCOV_COVERAGE_DUMP_START and GCOV_COVERAGE_DUMP_END: one `*<gcda path><hex>`
record per instrumented file. The host writes the .gcda files beside copies of
the build's .gcno files and reads them with the SDK's gcov (GCC 14 JSON).

Keys are relative to the application (`src/main.c`): lines `(file, line)` and
branch outcomes `(file, line, index)`, exception edges left out.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

MODULE = Path(__file__).resolve().parent / "gcov_module"
COVERAGE_CMAKE = [
    "-DCONFIG_DEBUG_OPTIMIZATIONS=y",
    "-DCONFIG_COVERAGE=y", "-DCONFIG_FORCE_COVERAGE=y",
    "-DCONFIG_COVERAGE_GCOV=y", "-DCONFIG_COVERAGE_DUMP=y",
    f"-DEXTRA_ZEPHYR_MODULES={MODULE}",
]
DUMP = re.compile(r"GCOV_COVERAGE_DUMP_START(.*?)GCOV_COVERAGE_DUMP_END", re.S)
RECORD = re.compile(r"\*([^<\n]+)<([0-9a-fA-F]*)")
SOURCE_SUFFIXES = {".c", ".h"}
GCOV_TIMEOUT_S = 60


def gcov_tool():
    sdk = os.environ.get("ZEPHYR_SDK_INSTALL_DIR")
    if not sdk:
        raise SystemExit("set ZEPHYR_SDK_INSTALL_DIR: gcov comes from the Zephyr SDK")
    tool = Path(sdk) / "gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcov"
    if not tool.is_file():
        raise SystemExit(f"{tool} not found")
    return tool


def parse_dumps(text):
    """[{gcda path: bytes}] for every complete dump in a console text."""
    dumps = []
    for body in DUMP.findall(text):
        records = {}
        for name, data in RECORD.findall(body):
            if len(data) % 2:
                raise ValueError(f"truncated gcov record for {name}")
            records[name] = bytes.fromhex(data)
        dumps.append(records)
    return dumps


def _gcov(tool, data_file, cwd):
    result = subprocess.run([str(tool), "--json-format", "--stdout", "--branch-probabilities",
                             str(data_file)], cwd=cwd, capture_output=True, text=True,
                            timeout=GCOV_TIMEOUT_S)
    if result.returncode != 0:
        raise SystemExit(f"gcov failed on {data_file}: {result.stderr.strip()}")
    return [json.loads(line) for line in result.stdout.splitlines() if line.strip()]


def _relative(app, path, cwd):
    """`src/x.c` for a source under app/src, else None."""
    source_root = Path(os.path.realpath(Path(app) / "src"))
    path = Path(path) if os.path.isabs(path) else Path(cwd) / path
    path = Path(os.path.realpath(path))
    if path.suffix not in SOURCE_SUFFIXES or not path.is_relative_to(source_root):
        return None
    return path.relative_to(source_root.parent).as_posix()


def _read(app, documents):
    """Line counts and branch-outcome counts of app/src from gcov JSON."""
    lines, branches = {}, {}
    for document in documents:
        cwd = document.get("current_working_directory", "")
        for entry in document["files"]:
            name = _relative(app, entry["file"], cwd)
            if name is None:
                continue
            for line in entry["lines"]:
                key = (name, line["line_number"])
                lines[key] = lines.get(key, 0) + line["count"]
                outcomes = [b for b in line["branches"] if not b["throw"]]
                for index, branch in enumerate(outcomes):
                    bkey = (*key, index)
                    branches[bkey] = branches.get(bkey, 0) + branch["count"]
    return lines, branches


def notes_files(build):
    """The .gcno files of the application objects of one build directory."""
    return sorted(Path(build).glob("CMakeFiles/app.dir/**/*.gcno"))


def structure(app, build, work):
    """Every executable line and branch outcome of app/src in a build (no run)."""
    work = Path(work)
    work.mkdir(parents=True, exist_ok=True)
    tool, documents = gcov_tool(), []
    for notes in notes_files(build):
        copy = work / notes.relative_to(build)
        copy.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(notes, copy)
        documents += _gcov(tool, copy, work)
    shutil.rmtree(work, ignore_errors=True)
    return _read(app, documents)


def measured(app, build, dumps, work):
    """Summed counts of several dumps (one per machine run) of one build."""
    build, work = Path(build).resolve(), Path(work)
    tool = gcov_tool()
    lines, branches = {}, {}
    for number, records in enumerate(dumps):
        run = work / f"run-{number:04d}"
        documents = []
        for name, data in sorted(records.items()):
            try:
                relative = Path(os.path.realpath(name)).relative_to(Path(os.path.realpath(build)))
            except ValueError:
                continue  # not an object of this build
            notes = build / relative.with_suffix(".gcno")
            if not notes.is_file():
                continue
            target = run / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(notes, target.with_suffix(".gcno"))
            target.write_bytes(data)
            documents += _gcov(tool, target, run)
        run_lines, run_branches = _read(app, documents)
        for key, count in run_lines.items():
            lines[key] = lines.get(key, 0) + count
        for key, count in run_branches.items():
            branches[key] = branches.get(key, 0) + count
    shutil.rmtree(work, ignore_errors=True)
    return lines, branches


def hits(counts):
    return {key for key, count in counts.items() if count > 0}


def compatible_branches(reference, other):
    """Branch keys of `other` (another build of the same sources, such as a
    unit suite) that mean the same outcomes as in `reference`: lines whose
    number of outcomes is the same in both builds."""
    def per_line(keys):
        counts = {}
        for name, line, _ in keys:
            counts[(name, line)] = counts.get((name, line), 0) + 1
        return counts
    ref, oth = per_line(reference), per_line(other)
    return {key for key in other if ref.get(key[:2]) == oth.get(key[:2])}
