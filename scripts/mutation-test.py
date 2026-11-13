#!/usr/bin/env python3
"""Mutation score of the student firmware under real scenarios on Renode.

Needs a finished scripts/firmware-coverage.py run of the same, unchanged
sources (--coverage DIR). Each mutant changes one place in <app>/src. A copy
of the application is mutated, rebuilt, and run against the passing scenarios
that cover the mutated line; a scenario failure kills the mutant. A mutant on
a line no passing scenario reaches survives without running. Lines without a
DWARF address (#define values, initializers) run against every passing
scenario. Compile errors are invalid and assessments that stop without a
failing scenario are errors; neither counts.

score = killed / (killed + survived), after removing declared equivalents
(<app>/mutation-equivalent.json, at most 5% of the evaluated mutants).
"""
import argparse
import json
from pathlib import Path
import random
import re
import shutil
import sys
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from grading import mutants as mutation, workspace  # noqa: E402

EQUIVALENT_FILE = "mutation-equivalent.json"
EQUIVALENT_CAP = 0.05
STARTUP_S = 60


def parse_args(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--module", type=Path, required=True)
    ap.add_argument("--renode", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True, help="new directory under build/")
    ap.add_argument("--coverage", type=Path, required=True,
                    help="output directory of firmware-coverage.py for the same sources")
    ap.add_argument("--sample", type=int, default=60,
                    help="mutants to evaluate, chosen with --seed (default 60; 0 = all)")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--jobs", type=int, default=4, help="parallel mutants (default 4)")
    ap.add_argument("--min", type=float, help="exit 1 below this mutation score percent")
    return ap.parse_args(argv)


def candidates(app):
    found = []
    for path in sorted((app / "src").rglob("*")):
        if path.suffix in (".c", ".h") and path.is_file():
            name = path.relative_to(app).as_posix()
            found.extend(mutation.mutants(name, path.read_text()))
    return found


def choose(found, sample, seed):
    if sample <= 0 or sample >= len(found):
        return list(found)
    chosen = random.Random(seed).sample(range(len(found)), sample)
    return [found[i] for i in sorted(chosen)]


def equivalents(app):
    path = app / EQUIVALENT_FILE
    if not path.is_file():
        return {}, []
    entries = json.loads(path.read_text())
    declared, problems = {}, []
    for entry in entries if isinstance(entries, list) else []:
        if isinstance(entry, dict) and entry.get("id") and str(entry.get("reason", "")).strip():
            declared[entry["id"]] = entry["reason"].strip()
        else:
            problems.append(f"{EQUIVALENT_FILE}: every entry needs an id and a reason")
    if not isinstance(entries, list):
        problems.append(f"{EQUIVALENT_FILE} must be a JSON list")
    return declared, problems


def plan(mutant, coverage, executable_lines):
    """Scenarios to run for a mutant: covering passing ones, shortest first."""
    passing = [n for n, s in coverage["scenarios"].items() if s == "pass"]
    elapsed = coverage["scenario_elapsed_s"]
    key = (mutant.file, mutant.line)
    if key in executable_lines:
        names = coverage["map"].get(mutant.file, {}).get(str(mutant.line), [])
        names = [n for n in names if n in passing]
    else:
        names = passing
    return sorted(names, key=lambda n: (elapsed.get(n) or 0, n))


def score(results, declared, cap=EQUIVALENT_CAP):
    evaluated = [r for r in results if r["status"] in ("killed", "survived", "not_covered")]
    equivalent = [r for r in evaluated if r["id"] in declared and r["status"] != "killed"]
    notes = []
    if evaluated and len(equivalent) > cap * len(evaluated):
        notes.append(f"{len(equivalent)} declared equivalents exceed the {cap:.0%} cap; "
                     "none was applied")
        equivalent = []
    skip = {r["id"] for r in equivalent}
    killed = sum(r["status"] == "killed" for r in evaluated)
    survived = sum(r["status"] in ("survived", "not_covered") and r["id"] not in skip
                   for r in evaluated)
    total = killed + survived
    return {"killed": killed, "survived": survived, "equivalent": len(skip),
            "not_covered": sum(r["status"] == "not_covered" for r in evaluated),
            "invalid": sum(r["status"] == "invalid" for r in results),
            "error": sum(r["status"] == "error" for r in results),
            "score": round(100 * killed / total, 2) if total else 0.0, "notes": notes}


def load_coverage(directory):
    summary = json.loads((directory / "summary.json").read_text())
    summary["map"] = json.loads((directory / "coverage-map.json").read_text())
    if summary["sources"] != workspace.source_hashes(Path(summary["app"])):
        raise SystemExit("sources changed since the coverage run; run firmware-coverage.py again")
    return summary


def executable(coverage):
    lines = set()
    for name, entry in coverage["files"].items():
        hit = coverage["map"].get(name, {})
        lines.update((name, int(n)) for n in hit)
        lines.update((name, n) for n in entry["uncovered"])
    return lines


def verdict(code, records):
    """killed: a scenario failed (firmware failure or unfinished) or timed out.
    error: the assessment stopped without a failing scenario (harness
    problem); it is reported and does not count. survived: all passed."""
    if code == -9:
        return {"status": "killed", "killed_by": "timeout"}
    for record in records:
        failure = record.get("failure") or {}
        if failure.get("kind") == "harness_failure":
            return {"status": "error", "error": failure.get("message")}
        if not workspace.passed(record):
            return {"status": "killed", "killed_by": record["name"]}
    if code != 0:
        return {"status": "error", "error": f"assessment exited {code} without a failing scenario"}
    return {"status": "survived"}


def evaluate(args, output, coverage, chosen):
    """Rebuild and run mutants on --jobs workers; returns per-mutant results.

    Each worker owns a copy of the application and a build directory, which
    later mutants rebuild incrementally (only the mutated file changes).
    """
    app = Path(coverage["app"])
    lines = executable(coverage)
    scenario_dirs = [Path(d) for d in coverage["scenarios_dirs"]]
    module, renode = args.module.resolve(), args.renode.resolve()
    results, queue = [], []
    for mutant in chosen:
        names = plan(mutant, coverage, lines)
        if names:
            queue.append((mutant, names))
        else:
            results.append(dict(id=mutant.id, file=mutant.file, line=mutant.line, op=mutant.op,
                                status="not_covered", scenarios=[]))
    lock = threading.Lock()

    def worker(index):
        work = output / f"worker-{index}"
        shutil.copytree(app, work / "app")
        build, prepare, _ = workspace.lab.commands(module, renode, work, headless="all",
                                                   app=work / "app",
                                                   cmake_args=coverage["cmake_args"])
        prepared = False
        while True:
            with lock:
                if not queue:
                    return
                mutant, names = queue.pop(0)
            original = (app / mutant.file).read_text()
            (work / "app" / mutant.file).write_text(mutant.apply(original))
            result = dict(id=mutant.id, file=mutant.file, line=mutant.line, op=mutant.op,
                          scenarios=names)
            code = workspace.run_one(build, work / "build.log", workspace.BUILD_TIMEOUT_S)
            if code == 0 and not prepared:
                code = workspace.run_one(prepare, work / "prepare.log",
                                         workspace.PREPARE_TIMEOUT_S)
                prepared = code == 0
            if code != 0:
                result["status"] = "invalid"
            else:
                limit = STARTUP_S + 2 * sum(coverage["scenario_elapsed_s"].get(n) or 60
                                            for n in names)
                run = work / "run"
                code = workspace.run_one(
                    workspace.assess_command(module, renode, work / "firmware/boot.resc", run,
                                             names, scenario_dirs),
                    work / "run.log", limit)
                result.update(verdict(code, workspace.records(run)))
                shutil.rmtree(run, ignore_errors=True)
                log = output / "logs" / (re.sub(r"[^\w.-]", "_", mutant.id) + ".log")
                log.parent.mkdir(exist_ok=True)
                shutil.move(work / "run.log", log)
                result["log"] = str(log.relative_to(output))
            # Restore the file so this worker's next mutant starts clean.
            (work / "app" / mutant.file).write_text(original)
            with lock:
                results.append(result)
                print(f"{len(results)}/{len(chosen)} {result['status']:<9} {mutant.id}",
                      flush=True)

    threads = [threading.Thread(target=worker, args=(i,), daemon=True)
               for i in range(min(max(1, args.jobs), len(queue)))]
    try:
        for thread in threads:
            thread.start()
        for thread in threads:
            while thread.is_alive():
                thread.join(0.5)
    finally:
        with lock:
            queue.clear()
        workspace.kill_children()
    return results


def main(argv=None):
    args = parse_args(argv)
    output = workspace.new_output(args.output)
    workspace.select_environment(args.module)
    coverage = load_coverage(args.coverage.resolve())
    app = Path(coverage["app"])
    found = candidates(app)
    chosen = choose(found, args.sample, args.seed)
    declared, problems = equivalents(app)
    began = time.monotonic()
    results = evaluate(args, output, coverage, chosen)
    figures = score(results, {} if problems else declared)
    figures["notes"] = problems + figures["notes"]
    figures.update(v=1, app=str(app), candidates=len(found), sample=len(chosen), seed=args.seed,
                   elapsed_s=round(time.monotonic() - began, 1))
    (output / "summary.json").write_text(json.dumps(figures, indent=2) + "\n")
    (output / "mutants.json").write_text(json.dumps(results, indent=1) + "\n")
    for worker in output.glob("worker-*"):
        shutil.rmtree(worker, ignore_errors=True)
    sources = {}
    for result in sorted(results, key=lambda r: r["id"]):
        if result["status"] in ("survived", "not_covered", "error") and result["id"] not in declared:
            text = sources.setdefault(result["file"], (app / result["file"]).read_text().splitlines())
            print(f"{result['status']:<11} {result['id']}  | {text[result['line'] - 1].strip()[:70]}")
    print(f"score {figures['score']:.2f}% = {figures['killed']} killed / "
          f"{figures['killed'] + figures['survived']} evaluated "
          f"({figures['invalid']} invalid, {figures['error']} error, "
          f"{figures['equivalent']} equivalent, "
          f"{figures['not_covered']} not covered; {len(chosen)} of {len(found)} mutants, "
          f"seed {args.seed})")
    for note in figures["notes"]:
        print("note:", note)
    print(f"Details: {output / 'mutants.json'}")
    return 1 if args.min is not None and figures["score"] < args.min else 0


if __name__ == "__main__":
    raise SystemExit(main())
