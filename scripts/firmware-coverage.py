#!/usr/bin/env python3
"""Line coverage of the student firmware under real scenarios on Renode.

Builds the application with CONFIG_DEBUG_OPTIMIZATIONS (stable line mapping),
runs each scenario in its own Renode process while models/BlockCoverage.cs
records executed blocks, and maps them to lines of <app>/src through DWARF.
Only scenarios that pass count: a line run by a failing scenario is not
evidence of tested behaviour (the inert starter would otherwise score high).
Students measure the public fixtures; the instructor adds the hidden set with
--scenarios-dir, so a student's figure is a lower bound of the graded one.
"""
import argparse
import json
from pathlib import Path
import shutil
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from grading import linecov, workspace  # noqa: E402

COVERAGE_CMAKE = ["-DCONFIG_DEBUG_OPTIMIZATIONS=y"]
SCENARIO_TIMEOUT_S = 300


def parse_args(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--module", type=Path, required=True)
    ap.add_argument("--renode", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True, help="new directory under build/")
    ap.add_argument("--app", type=Path, default=workspace.ROOT / "student",
                    help="application to measure (default: student/)")
    ap.add_argument("--scenarios-dir", type=Path, action="append", default=[],
                    help="also run fixtures from this trusted directory")
    ap.add_argument("--scenario", action="append", default=[],
                    help="measure only these scenarios (default: all)")
    ap.add_argument("--cmake-arg", action="append", default=[],
                    help="extra CMake argument for the coverage build")
    ap.add_argument("--jobs", type=int, default=4, help="parallel Renode processes (default 4)")
    ap.add_argument("--min", type=float, help="exit 1 below this line coverage percent")
    return ap.parse_args(argv)


def measure(args, output):
    """Build, run every scenario with block recording; return the summary."""
    boot, error = workspace.build_firmware(args.module, args.renode, output, args.app,
                                           COVERAGE_CMAKE + args.cmake_arg)
    if error:
        raise SystemExit(error)
    names = workspace.scenario_names(args.scenarios_dir)
    for name in args.scenario:
        if name not in names:
            raise SystemExit(f"unknown scenario {name}")
    names = args.scenario or names
    jobs = [(workspace.assess_command(args.module.resolve(), args.renode.resolve(), boot,
                                      output / "scenarios" / name, [name],
                                      [d.resolve() for d in args.scenarios_dir],
                                      trace_dir=output / "blocks" / name),
             output / "scenarios" / f"{name}.log", SCENARIO_TIMEOUT_S) for name in names]
    began = time.monotonic()
    codes = workspace.run_parallel(jobs, args.jobs)
    elf = output / "firmware/zephyr/zephyr.elf"
    ranges = linecov.line_ranges(elf, args.app)
    hits, statuses = {}, {}
    for name, code in zip(names, codes):
        records = workspace.records(output / "scenarios" / name)
        statuses[name] = ("pass" if code == 0 and records and workspace.passed(records[0])
                          else "timeout" if code == -9 else "fail")
        blocks = linecov.read_blocks(sorted((output / "blocks" / name).glob("*.txt")))
        hits[name] = linecov.hit_lines(ranges, linecov.instruction_addresses(elf, blocks))
        # Block lists are small, but the per-scenario detail is not needed again.
        shutil.rmtree(output / "blocks" / name, ignore_errors=True)
    excluded, problems = linecov.exclusions(args.app)
    counted = {name: lines for name, lines in hits.items() if statuses[name] == "pass"}
    summary, line_map = linecov.summarize(ranges, counted, excluded, problems)
    summary.update(v=1, app=str(args.app.resolve()), elapsed_s=round(time.monotonic() - began, 1),
                   scenarios=statuses,
                   scenario_lines={name: len(lines) for name, lines in sorted(hits.items())})
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    (output / "coverage-map.json").write_text(json.dumps(line_map, indent=1) + "\n")
    linecov.write_lcov(output / "coverage.info", args.app, summary, line_map)
    shutil.rmtree(output / "blocks", ignore_errors=True)
    return summary


def report(summary):
    print(f"{'file':<28} {'lines':>6} {'hit':>6} {'%':>7}")
    for name, entry in sorted(summary["files"].items()):
        print(f"{name:<28} {entry['executable']:>6} {entry['hit']:>6} {entry['percent']:>7.2f}")
    print(f"{'total':<28} {summary['executable']:>6} {summary['hit']:>6} {summary['percent']:>7.2f}")
    failed = [n for n, s in summary["scenarios"].items() if s != "pass"]
    if failed:
        print("scenarios that did not pass (their lines are not counted):", ", ".join(failed))
    for note in summary["notes"]:
        print("note:", note)


def main(argv=None):
    args = parse_args(argv)
    output = workspace.new_output(args.output)
    args.app = args.app.resolve()
    workspace.select_environment(args.module)
    summary = measure(args, output)
    report(summary)
    print(f"Details: {output / 'summary.json'}; LCOV: {output / 'coverage.info'}")
    return 1 if args.min is not None and summary["percent"] < args.min else 0


if __name__ == "__main__":
    raise SystemExit(main())
