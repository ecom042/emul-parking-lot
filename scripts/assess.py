#!/usr/bin/env python3
"""Exercise all shared scenarios on official Renode without Robot/TCP services."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time


def main():
    root = Path(__file__).resolve().parents[1]
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--module", type=Path, required=True)
    ap.add_argument("--renode", type=Path, required=True)
    ap.add_argument("--machine-script", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--scenario", action="append", default=[],
                    help="run this fixture; repeat for several, in order (default: all)")
    ap.add_argument("--scenarios-dir", type=Path, action="append", default=[],
                    help="also load fixtures from this trusted directory (instructor hidden set)")
    ap.add_argument("--trace-dir", type=Path,
                    help="write the executed blocks of each machine run here (coverage only)")
    ap.add_argument("--lot", type=int, metavar="SECONDS",
                    help="run the car queue for this many virtual seconds instead")
    ap.add_argument("--cars", type=Path,
                    help="JSON list of car profiles for --lot (default: five normal cars)")
    args = ap.parse_args()
    if args.lot is not None and not 1 <= args.lot <= 3600:
        ap.error("--lot must be 1..3600 virtual seconds")
    if args.cars and args.lot is None:
        ap.error("--cars requires --lot")
    scenario_dirs = [root / "tests/scenarios", *(d.resolve() for d in args.scenarios_dir)]
    names = [p.stem for d in scenario_dirs for p in d.glob("*.json")]
    if len(names) != len(set(names)):
        ap.error("scenario names must be unique across --scenarios-dir")
    for name in args.scenario:
        if name not in names:
            ap.error(f"unknown scenario {name}")
    environment = {"PARKING_BOOT": str(args.machine_script.resolve()),
                   "PARKING_RESET": str(args.machine_script.resolve().with_name("reset.resc"))}
    if args.trace_dir:
        args.trace_dir.mkdir(parents=True, exist_ok=False)
        environment["PARKING_TRACE_DIR"] = str(args.trace_dir.resolve())
        environment["PARKING_COVERAGE_MODEL"] = str(root / "models/BlockCoverage.cs")
    sys.path.insert(0, str(args.module.resolve() / "src"))
    from zvb.stdio import StdioRuntime
    from parking_web import ParkingEngine, built_config
    args.output.mkdir(parents=True, exist_ok=False)
    boot = args.machine_script.resolve()
    runtime = StdioRuntime(args.renode.resolve(), root / "scripts/parking_embedded.py",
                           own_session=False, environment=environment)
    engine = ParkingEngine(runtime, scenario_dirs, args.output / "runs",
                           pace=0, step_ticks=100, **built_config(boot))
    results = []
    def command(op, **fields):
        return engine.command({"v": 1, "run": engine.snapshot()["run"],
                               "id": str(time.monotonic_ns()), "op": op, **fields})
    try:
        if args.lot is not None:
            return lot(args, engine, command, boot)
        for name in (args.scenario or engine.scenarios):
            began = time.monotonic()
            command("select", scenario=name)
            command("start")
            deadline = began + 120
            while time.monotonic() < deadline:
                state = engine.snapshot()
                if state["finished"] or state["failure"]:
                    break
                time.sleep(0.05)
            record = {"name": name, "finished": state["finished"], "failure": state["failure"],
                      "elapsed": time.monotonic() - began}
            results.append(record)
            print(json.dumps(record), flush=True)
            (args.output / "results.json").write_text(json.dumps({
                "v": 1, "argv": sys.argv, "results": results,
                "boot_sha256": hashlib.sha256(boot.read_bytes()).hexdigest()}, indent=2) + "\n")
            if not state["finished"] or state["failure"]:
                return 1
    finally:
        engine.close()
    return 0


def lot(args, engine, command, boot):
    """Drive the car queue headless; pass only when every verdict passes."""
    from scenario import driver
    profiles = json.loads(args.cars.read_text()) if args.cars else None
    if profiles is not None:
        if type(profiles) is not list or not 1 <= len(profiles) <= driver.MAX_CARS:
            raise SystemExit("--cars must be a JSON list of 1..64 profiles")
        for car in list(engine.driver.cars):
            command("car.remove", car=car)
        for profile in profiles:
            command("car.add", profile=profile)
    command("start")
    target = args.lot * 100
    began = time.monotonic()
    while time.monotonic() < began + max(120, args.lot * 4):
        state = engine.snapshot()
        if state["failure"] or state["lanes"]["entry"]["physical"]["tick"] >= target:
            break
        time.sleep(0.05)
    command("pause")
    state = engine.snapshot()
    queue = state["queue"]
    verdicts = [v for v in queue["verdicts"] if v["status"] != "info"]
    record = {"name": "lot", "seconds": args.lot, "failure": state["failure"],
              "tick": state["lanes"]["entry"]["physical"]["tick"],
              "cars": queue["cars"], "verdicts": verdicts, "totals": queue["totals"],
              "halted": queue["halted"],
              "boot_sha256": hashlib.sha256(boot.read_bytes()).hexdigest()}
    (args.output / "results.json").write_text(json.dumps(record, indent=2) + "\n")
    # The view keeps only recent verdicts; the totals cover the whole run.
    passed, total = queue["totals"]["pass"], sum(queue["totals"].values())
    print(json.dumps({"name": "lot", "failure": state["failure"], "tick": record["tick"],
                      "verdicts": f"{passed}/{total} pass",
                      "halted": queue["halted"]}), flush=True)
    return 0 if not state["failure"] and total and passed == total else 1


if __name__ == "__main__":
    raise SystemExit(main())
