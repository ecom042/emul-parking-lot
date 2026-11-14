#!/usr/bin/env python3
"""Line coverage of the student firmware under real scenarios on Renode.

Builds the application with CONFIG_DEBUG_OPTIMIZATIONS (stable line mapping),
runs each scenario in its own Renode process while models/BlockCoverage.cs
records executed blocks, and maps them to lines of <app>/src through DWARF.
Unit-test suites in <app>/tests/<suite>/ (ztest + FFF, see grading/unit.py)
run on the same board in Renode and count as well, named "unit:<suite>".
Only scenarios and suites that pass count: a line run by a failing one is not
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
from grading import linecov, unit, workspace  # noqa: E402

COVERAGE_CMAKE = ["-DCONFIG_DEBUG_OPTIMIZATIONS=y"]


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
    ap.add_argument("--exclude-branches-by-pattern", "--exclude-lines-by-pattern",
                    dest="exclude_pattern", action="append", default=[], metavar="REGEX",
                    help="drop source lines whose whole text matches REGEX (repeatable; "
                         "gcovr-compatible name, applied to lines: there are no branches)")
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
    began = time.monotonic()
    statuses, runs = workspace.scenario_statuses(args.module, args.renode, boot,
                                                 output / "scenarios", names, args.scenarios_dir,
                                                 args.jobs, trace_root=output / "blocks")
    elf = output / "firmware/zephyr/zephyr.elf"
    ranges = linecov.line_ranges(elf, args.app)
    hits, elapsed = {}, {}
    for name in names:
        run, trace = runs[name]
        records = workspace.records(run)
        elapsed[name] = round(records[0]["elapsed"], 1) if records else None
        blocks = linecov.read_blocks(sorted(trace.glob("*.txt")))
        hits[name] = linecov.hit_lines(ranges, linecov.instruction_addresses(elf, blocks))
    for suite in unit.suites(args.app):
        name = f"unit:{suite.name}"
        work = output / "unit" / suite.name
        started = time.monotonic()
        job, error = unit.prepare(args.module, args.renode, work, suite,
                                  COVERAGE_CMAKE + args.cmake_arg)
        code = workspace.run_one(*job) if job else 1
        status, failed = unit.outcome(work, code) if job else ("build failed", [])
        statuses[name] = "pass" if status == "pass" else status
        elapsed[name] = round(time.monotonic() - started, 1)
        if failed:
            print(f"{name}: failing tests: {', '.join(failed)}")
        if job:
            suite_elf = work / "firmware/zephyr/zephyr.elf"
            blocks = linecov.read_blocks([work / "blocks.txt"]) if (work / "blocks.txt").is_file() else set()
            hits[name] = linecov.hit_lines(linecov.line_ranges(suite_elf, args.app),
                                           linecov.instruction_addresses(suite_elf, blocks))
        else:
            hits[name] = set()
            print(f"{name}: {error}")
    excluded, problems = linecov.exclusions(args.app)
    counted = {name: lines for name, lines in hits.items() if statuses[name] == "pass"}
    by_pattern = linecov.pattern_exclusions(args.app, args.exclude_pattern)
    summary, line_map = linecov.summarize(ranges, counted, excluded, problems,
                                          by_pattern=by_pattern)
    summary.update(v=1, app=str(args.app.resolve()), elapsed_s=round(time.monotonic() - began, 1),
                   sources=workspace.source_hashes(args.app),
                   cmake_args=COVERAGE_CMAKE + args.cmake_arg,
                   exclude_patterns=args.exclude_pattern,
                   scenarios_dirs=[str(d.resolve()) for d in args.scenarios_dir],
                   scenarios=statuses, scenario_elapsed_s=elapsed,
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
        print("scenarios/suites that did not pass (their lines are not counted):",
              ", ".join(failed))
    if summary.get("excluded_by_pattern"):
        print(f"{summary['excluded_by_pattern']} executable line(s) excluded by pattern")
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
