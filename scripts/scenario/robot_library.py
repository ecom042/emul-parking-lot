"""Privileged Robot adapter for the typed parking scenario boundary."""

import json
from pathlib import Path

from scenario.parser import load
from scenario.assessment import FirmwareFailure
from scenario.observations import charge_uuids
from scenario import runner

__all__ = ["ScenarioLibrary", "RobotTransport", "charge_uuids"]

# Fixed owner table, mirroring parking_embedded.PATHS.
PATHS = {
    "entry.physical": "sysbus.gpiob.physicalEntry",
    "entry.protocol": "sysbus.gpioe.protocolsEntry",
    "exit.physical": "sysbus.gpiob.physicalExit",
    "exit.protocol": "sysbus.gpiod.protocolsExit",
}


class RobotTransport:
    """Implement the runner's typed request boundary with monitor commands."""

    def __init__(self, command, machine_script):
        self.command = command
        self.machine_script = Path(machine_script)
        self.sequences = {owner: 0 for owner in PATHS}

    def include(self, path):
        self.command("include @" + str(Path(path).resolve()).replace(" ", "\\ "))

    def state(self):
        lanes = {lane: {} for lane in runner.LANES}
        events = {}
        for owner, path in PATHS.items():
            lane, kind = owner.split(".")
            lanes[lane][kind] = json.loads(self.command(f"{path} Snapshot"))
            trace = json.loads(self.command(f"{path} TraceAfter {self.sequences[owner]}"))
            events[owner] = trace
            if trace:
                self.sequences[owner] = trace[-1]["seq"]
        return {"lanes": lanes, "events": events}

    def request(self, payload):
        op = payload["op"]
        protocol = lambda lane: PATHS[f"{lane}.protocol"]
        if op == "init":
            self.include(self.machine_script)
        elif op == "reset":
            self.include(self.machine_script.with_name("reset.resc"))
            self.sequences = {owner: 0 for owner in PATHS}
        elif op == "initial":
            self.command(f'{PATHS[payload["lane"] + ".physical"]} ConfigureInitial '
                         f'{payload["position_mdeg"]} {payload["vehicle_mm"]} {payload["seed"]}')
        elif op == "configure":
            config, lane = payload["config"], payload["lane"]
            missing = str(config["missing"]).lower()
            if payload["kind"] == "camera":
                confidence = ",".join(map(str, config["confidences"]))
                self.command(f'{protocol(lane)} ConfigureCamera "{config["plate"]}" '
                             f'"{confidence}" {config["delay_ticks"]} '
                             f'{config["fragment_bytes"]} {missing}')
            else:
                self.command(f'{protocol(lane)} ConfigurePayment {config["delay_ticks"]} '
                             f'{config["fragment_bytes"]} "{config["result"]}" {missing}')
        elif op == "step":
            self.command(f'emulation RunFor "{payload["ticks"] / 100:.2f}"')
        elif op == "submit":
            c = payload["command"]
            return json.loads(self.command(
                f'{PATHS[payload["owner"]]} Submit {c["v"]} {c["run"]} {c["id"]} '
                f'{c["at_tick"]} "{c["signal"]}" {c["value"]}'))
        elif op == "replay":
            self.command(f'{protocol("exit")} ReplayPayment "{payload["uuid"]}" '
                         f'{payload["delay_ticks"]}')
        elif op != "snapshot":
            raise ValueError("unknown operation")
        return self.state()


def built_capacity(machine_script):
    config = Path(machine_script).parent / "zephyr/.config"
    for line in config.read_text().splitlines():
        if line.startswith("CONFIG_PARKING_CAPACITY="):
            return int(line.split("=", 1)[1])
    raise RuntimeError("trusted build has no CONFIG_PARKING_CAPACITY")


class ScenarioLibrary:
    ROBOT_LIBRARY_SCOPE = "SUITE"

    def execute_scenario(self, path, machine_script, result_path):
        """Always leave structured evidence, including on transport/setup failures."""
        output = Path(result_path)
        result = {"version": 2, "status": "harness_failure", "scenario": str(path), "runs": []}
        run = None
        try:
            data = load(path)
            from robot.libraries.BuiltIn import BuiltIn
            robot = BuiltIn()
            result["name"] = data["name"]
            result["seed"] = data["seed"]
            robot.run_keyword("Reset Emulation")
            transport = RobotTransport(
                lambda command: robot.run_keyword("Execute Command", command), machine_script)
            capacity = built_capacity(machine_script)
            state = transport.request({"op": "init"})
            for index, spec in enumerate(data["runs"]):
                if index:
                    state = runner.reset(transport.request, state)
                record = {"index": index, "events_file": f"run-{index}.events.jsonl.gz",
                          "checkpoints": []}
                result["runs"].append(record)
                output.parent.mkdir(parents=True, exist_ok=True)
                run = runner.RunController(
                    transport.request, spec, state, seed=data["seed"], capacity=capacity,
                    motor_ms=data["deadlines"]["motor_ms"], step_ticks=100,
                    evidence=output.parent / record["events_file"],
                    required_capacity=data.get("capacity"))
                record["source_runs"] = {o: s.run for o, s in run.streams.items()}
                try:
                    while not run.advance():
                        pass
                finally:
                    record["checkpoints"] = list(run.results)
                    record["end"] = run.state["lanes"]
                    record["event_counts"] = {o: s.count for o, s in run.streams.items()}
                    run.close()
                state = run.state
            result["status"] = "pass"
        except FirmwareFailure as error:
            result.update(status="firmware_failure", reason=str(error))
        except Exception as error:
            result.update(status="harness_failure", reason=f"{type(error).__name__}: {error}")
        finally:
            if run:
                run.close()
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(json.dumps(result, indent=2) + "\n")
        if result["status"] != "pass":
            raise AssertionError(f'{result["status"]}: {result.get("reason")}')
