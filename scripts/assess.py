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
    ap.add_argument("--scenario", choices=[p.stem for p in (root / "tests/scenarios").glob("*.json")],
                    help="run one retained fixture (default: all)")
    args = ap.parse_args()
    sys.path.insert(0, str(args.module.resolve() / "src"))
    from zvb.stdio import StdioRuntime
    from parking_web import ParkingEngine
    args.output.mkdir(parents=True, exist_ok=False)
    boot = args.machine_script.resolve()
    runtime = StdioRuntime(args.renode.resolve(), root / "scripts/parking_embedded.py",
                           own_session=False,
                           environment={"PARKING_BOOT": str(boot),
                                        "PARKING_RESET": str(boot.with_name("reset.resc"))})
    engine = ParkingEngine(runtime, root / "tests/scenarios", args.output / "runs",
                           pace=0, step_ticks=100)
    results = []
    def command(op, **fields):
        return engine.command({"v": 1, "run": engine.snapshot()["run"],
                               "id": str(time.monotonic_ns()), "op": op, **fields})
    try:
        for name in ([args.scenario] if args.scenario else engine.scenarios):
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


if __name__ == "__main__":
    raise SystemExit(main())
