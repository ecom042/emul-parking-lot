"""Run strict JSON parking scenarios with official Robot and a finite watchdog."""

import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import sys

from scenario.parser import load
from scenario.outcome import infrastructure_error

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scenarios", nargs="+", type=Path)
    parser.add_argument("--renode-root", type=Path, required=True)
    parser.add_argument("--machine-script", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True,
                        help="new evidence directory; never reuses stale assessment files")
    parser.add_argument("--timeout", type=float, default=120,
                        help="finite host watchdog seconds per scenario (1..3600)")
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or not 1 <= args.timeout <= 3600:
        parser.error("--timeout must be finite and within 1..3600")
    aggregate = {"version": 1, "status": "harness_failure", "results": []}
    output = None
    try:
        args.output.mkdir(parents=True, exist_ok=False)
        output = args.output.resolve()
        (output / "results.json").write_text(json.dumps(aggregate, indent=2) + "\n")
        if len(args.scenarios) > 64:
            raise ValueError("at most 64 scenarios per invocation")
        scenarios = [(path.resolve(), load(path)) for path in args.scenarios]
        renode = args.renode_root.resolve()
        machine = args.machine_script.resolve()
        if not (renode / "tests/run_tests.py").is_file() or not machine.is_file():
            raise ValueError("official Renode tests/run_tests.py and prepared machine script required")
        aggregate["machine_script"] = {"path": str(machine), "sha256": digest(machine)}
        spec = importlib.util.spec_from_file_location("bounded_command", ROOT / "scripts/bounded-command.py")
        bounded = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(bounded)
        # Official Robot inherits these paths; no global installs or web server.
        os.environ["PYTHONPATH"] = os.pathsep.join(
            [str(ROOT / "scripts"), str(ROOT / "tests"), os.environ.get("PYTHONPATH", "")])
        for index, (path, data) in enumerate(scenarios):
            evidence = output / f'{index:02d}-{data["name"]}'
            evidence.mkdir()
            # Freeze validated stimuli for this attempt: a concurrent author edit
            # must not change the scenario between preflight and Robot execution.
            frozen = evidence / "scenario.json"
            frozen.write_text(json.dumps(data, indent=2) + "\n")
            result_path = evidence / "assessment.json"
            command = [
                sys.executable, str(renode / "tests/run_tests.py"),
                str(ROOT / "tests/scenario.robot"),
                "--variable", f"MACHINE_SCRIPT:{machine}",
                "--variable", f"SCENARIO:{frozen}",
                "--variable", f"SCENARIO_RESULT:{result_path}",
                "--results-dir", str(evidence / "robot"),
                "--test-timeout", f"{max(1, args.timeout - 5):g} seconds",
                "--keep-renode-output", "--save-logs", "always",
                "--css-file", str(renode / "tests/robot.css"),
                "--robot-framework-remote-server-full-directory", str(renode),
                "--robot-framework-remote-server-name", "renode",
            ]
            status = bounded.run(command, args.timeout, evidence / "runner.log")
            record = {"name": data["name"], "scenario": str(path),
                      "executed_scenario": str(frozen), "sha256": digest(frozen),
                      "status": "harness_failure",
                      "assessment": str(result_path), "returncode": status}
            if result_path.is_file():
                assessment = json.loads(result_path.read_text())
                record["status"] = assessment["status"]
                record["reason"] = assessment.get("reason")
                # A timeout, teardown failure or unrelated Robot failure cannot pass.
                wanted_returncode = {"pass": 0, "firmware_failure": 1}.get(record["status"])
                if wanted_returncode is None or status != wanted_returncode:
                    record.update(status="harness_failure", reason="official runner/watchdog failed")
                elif record["status"] in ("pass", "firmware_failure"):
                    error = infrastructure_error(evidence / "robot", record["status"])
                    if error:
                        record.update(status="harness_failure", reason=error)
            else:
                record["reason"] = "official runner produced no assessment"
            aggregate["results"].append(record)
            (output / "results.json").write_text(json.dumps(aggregate, indent=2) + "\n")
            if status in (130, 143):
                aggregate["reason"] = "assessment cancelled; remaining scenarios not started"
                break
        statuses = {r["status"] for r in aggregate["results"]}
        aggregate["status"] = ("harness_failure" if "harness_failure" in statuses else
                               "firmware_failure" if "firmware_failure" in statuses else "pass")
    except Exception as error:
        aggregate.update(status="harness_failure", reason=f"{type(error).__name__}: {error}")
    if output is not None:
        (output / "results.json").write_text(json.dumps(aggregate, indent=2) + "\n")
    print(json.dumps(aggregate, indent=2))
    return {"pass": 0, "firmware_failure": 1, "harness_failure": 2}[aggregate["status"]]


if __name__ == "__main__":
    raise SystemExit(main())
