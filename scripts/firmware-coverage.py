#!/usr/bin/env python3
"""Line and branch coverage of the student firmware under real scenarios.

Builds the application for gcov (grading/gcov.py: only the application is
instrumented, with CONFIG_DEBUG_OPTIMIZATIONS), runs each scenario in its own
Renode process, collects the gcov dump of every machine run (at the end and
before each reset) and reads lines and branch outcomes of <app>/src with the
SDK's gcov.
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
from grading import gcov, linecov, unit, workspace  # noqa: E402

COVERAGE_CMAKE = gcov.COVERAGE_CMAKE


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
                    help="drop source lines whose whole text matches REGEX, with their "
                         "branches (repeatable; as gcovr)")
    ap.add_argument("--jobs", type=int, default=workspace.default_jobs(),
                    help="parallel Renode processes (default: up to 4, about 2 GB of RAM each)")
    ap.add_argument("--min", type=float,
                    help="exit 1 when line or branch coverage is below this percent")
    return ap.parse_args(argv)


def measure(args, output):
    """Build, run every scenario with gcov dumps; return the summary."""
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
                                                 args.jobs, trace_root=output / "gcov")
    build = output / "firmware"
    lines, branches = gcov.structure(args.app, build, output / "gcov-work/structure")
    line_hits, branch_hits, elapsed, notes = {}, {}, {}, []
    for name in names:
        run, trace = runs[name]
        records = workspace.records(run)
        elapsed[name] = round(records[0]["elapsed"], 1) if records else None
        dumps = [d for path in sorted(trace.glob("dump-*.txt"))
                 for d in gcov.parse_dumps(path.read_text())]
        if statuses[name] == "pass" and not dumps:
            notes.append(f"{name}: passed but the firmware gave no gcov dump; not counted")
        counts = gcov.measured(args.app, build, dumps, output / "gcov-work" / name)
        line_hits[name], branch_hits[name] = (gcov.hits(c) for c in counts)
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
            dumps = unit.dumps(work)
            if statuses[name] == "pass" and not dumps:
                notes.append(f"{name}: passed but gave no gcov dump; not counted")
            suite_lines, suite_branches = gcov.measured(
                args.app, work / "firmware", dumps, output / "gcov-work" / name.replace(":", "-"))
            line_hits[name] = gcov.hits(suite_lines)
            # Only outcomes that mean the same in the firmware build count.
            branch_hits[name] = gcov.hits(suite_branches) & gcov.compatible_branches(
                branches, suite_branches)
        else:
            line_hits[name], branch_hits[name] = set(), set()
            print(f"{name}: {error}")
    excluded, problems = linecov.exclusions(args.app)
    passing = [name for name in line_hits if statuses[name] == "pass"]
    by_pattern = linecov.pattern_exclusions(args.app, args.exclude_pattern)
    summary, line_map, branch_map = linecov.summarize(
        lines, {n: line_hits[n] for n in passing}, excluded, problems, by_pattern=by_pattern,
        branches=set(branches), branch_hits={n: branch_hits[n] for n in passing})
    summary["notes"] += notes
    summary.update(v=2, app=str(args.app.resolve()), elapsed_s=round(time.monotonic() - began, 1),
                   sources=workspace.source_hashes(args.app),
                   cmake_args=COVERAGE_CMAKE + args.cmake_arg,
                   exclude_patterns=args.exclude_pattern,
                   scenarios_dirs=[str(d.resolve()) for d in args.scenarios_dir],
                   scenarios=statuses, scenario_elapsed_s=elapsed,
                   scenario_lines={name: len(hit) for name, hit in sorted(line_hits.items())},
                   scenario_branches={name: len(hit) for name, hit in sorted(branch_hits.items())})
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    (output / "coverage-map.json").write_text(json.dumps(line_map, indent=1) + "\n")
    (output / "branch-map.json").write_text(json.dumps(branch_map, indent=1) + "\n")
    linecov.write_lcov(output / "coverage.info", args.app, summary, line_map, branch_map)
    shutil.rmtree(output / "gcov", ignore_errors=True)
    shutil.rmtree(output / "gcov-work", ignore_errors=True)
    return summary


def report(summary):
    print(f"{'file':<22} {'lines':>6} {'hit':>5} {'%':>7}   {'branches':>8} {'hit':>5} {'%':>7}")
    rows = sorted(summary["files"].items()) + [("total", summary)]
    for name, e in rows:
        print(f"{name:<22} {e['executable']:>6} {e['hit']:>5} {e['percent']:>7.2f}   "
              f"{e['branches']:>8} {e['branches_hit']:>5} {e['branch_percent']:>7.2f}")
    failed = [n for n, s in summary["scenarios"].items()
              if s != "pass" and s not in workspace.HOST_STATUSES]
    host = [n for n, s in summary["scenarios"].items() if s in workspace.HOST_STATUSES]
    if failed:
        print("scenarios/suites that did not pass (their lines are not counted):",
              ", ".join(failed))
    if host:
        print("host problem even after a retry (time or memory; not a firmware verdict, lines"
              " not counted). Re-run with fewer --jobs:", ", ".join(host))
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
    low = min(summary["percent"], summary["branch_percent"])
    return 1 if args.min is not None and low < args.min else 0


if __name__ == "__main__":
    raise SystemExit(main())
