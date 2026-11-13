"""Privileged Robot adapter for the typed parking scenario boundary."""

import json
from pathlib import Path

from scenario.parser import load
from scenario.assessment import FirmwareFailure, HarnessFailure, Stream, assess, checkpoint
from scenario.observations import charge_uuids, frames

PHYSICAL = "sysbus.gpiob.physical"
PROTOCOL = "sysbus.gpiod.protocols"


class ScenarioLibrary:
    ROBOT_LIBRARY_SCOPE = "SUITE"

    def execute_scenario(self, path, machine_script, result_path):
        """Always leave structured evidence, including on transport/setup failures."""
        result = {"version": 1, "status": "harness_failure", "scenario": str(path), "runs": []}
        try:
            data = load(path)
            from robot.libraries.BuiltIn import BuiltIn
            self.robot = BuiltIn()
            result["name"] = data["name"]
            result["seed"] = data["seed"]
            self.robot.run_keyword("Reset Emulation")
            self.include(machine_script)
            previous = None
            for index, run in enumerate(data["runs"]):
                if index:
                    self.include(str(Path(machine_script).with_name("reset.resc")))
                record = {"index": index, "physical": [], "protocol": [], "checkpoints": []}
                result["runs"].append(record)
                previous = self.execute_run(data, run, record, previous)
            result["status"] = "pass"
        except FirmwareFailure as error:
            result.update(status="firmware_failure", reason=str(error))
        except Exception as error:
            result.update(status="harness_failure", reason=f"{type(error).__name__}: {error}")
        finally:
            output = Path(result_path)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(json.dumps(result, indent=2) + "\n")
        if result["status"] != "pass":
            raise AssertionError(f'{result["status"]}: {result.get("reason")}')

    def command(self, command):
        return self.robot.run_keyword("Execute Command", command)

    def include(self, path):
        self.command("include @" + str(Path(path).resolve()).replace(" ", "\\ "))

    def snapshot(self, device):
        return json.loads(self.command(f"{device} Snapshot"))

    def configure(self, kind, config):
        missing = str(config["missing"]).lower()
        if kind == "camera":
            confidence = ",".join(map(str, config["confidences"]))
            self.command(f'{PROTOCOL} ConfigureCamera "{config["plate"]}" "{confidence}" '
                         f'{config["delay_ticks"]} {config["fragment_bytes"]} {missing}')
        else:
            self.command(f'{PROTOCOL} ConfigurePayment {config["delay_ticks"]} '
                         f'{config["fragment_bytes"]} "{config["result"]}" {missing}')

    def execute_run(self, data, run, record, previous):
        physical, protocol = self.snapshot(PHYSICAL), self.snapshot(PROTOCOL)
        if physical["tick"] != 0 or protocol["tick"] != 0:
            raise HarnessFailure("initial configuration requires paused Tick 0")
        if physical["pending"] or protocol["pending"]:
            raise HarnessFailure("reset left pending equipment work")
        if previous:
            for device, state in ((PHYSICAL, physical), (PROTOCOL, protocol)):
                if state["run"] == previous[device]["run"]:
                    raise HarnessFailure("reset did not change run identity")
                signal = "sensor.noise" if device == PHYSICAL else "payment.replay"
                value = 0 if device == PHYSICAL else 1
                ack = json.loads(self.command(
                    f'{device} Submit 1 {previous[device]["run"]} 2147483647 1 '
                    f'"{signal}" {value}'))
                if ack.get("accepted") is not False or ack.get("error") != "stale_run":
                    raise HarnessFailure("old-run command was not rejected")
                if state["time_us"] < previous[device]["time_us"]:
                    raise HarnessFailure("machine source time moved backwards on reset")
        initial = run["initial"]
        self.command(f'{PHYSICAL} ConfigureInitial {initial["position_mdeg"]} '
                     f'{initial["vehicle_mm"]} {data["seed"]}')
        for kind in ("camera", "payment"):
            if kind in run:
                self.configure(kind, run[kind])
        streams = {
            PHYSICAL: Stream(physical["run"], physical["time_us"]),
            PROTOCOL: Stream(protocol["run"], protocol["time_us"]),
        }
        record.update(physical_run=physical["run"], protocol_run=protocol["run"],
                      start_time_us=physical["time_us"])
        # Keep live lists in the result even if a later command fails.
        record["physical"] = streams[PHYSICAL].events
        record["protocol"] = streams[PROTOCOL].events
        actions = run["actions"]
        expectations = run["expect"]
        # Submit actions one tick before their execution. Configuration helpers
        # execute paused at that same boundary, before the tick callback.
        boundaries = {0, run["duration_ticks"]}
        boundaries.update(a["at_tick"] - 1 for a in actions)
        boundaries.update(e["at_tick"] for e in expectations)
        boundaries.update(range(100, run["duration_ticks"], 100))
        now = 0
        command_id = 0
        for target in sorted(boundaries):
            if target > now:
                self.command(f'emulation RunFor "{(target - now) / 100:.2f}"')
                now = target
            states = {device: self.snapshot(device) for device in streams}
            for device, stream in streams.items():
                if states[device]["run"] != stream.run or states[device]["tick"] != now:
                    raise HarnessFailure("run/tick changed unexpectedly during finite step")
                stream.collect(json.loads(self.command(f"{device} Trace")))
            p, q = streams[PHYSICAL].events, streams[PROTOCOL].events
            record["end"] = states
            for event in p + q:
                if event["signal"] == "failed":
                    raise HarnessFailure(f"scheduled equipment command failed: {event}")
            assess(p, q, states[PHYSICAL], run["green"],
                   data["deadlines"]["motor_ms"], final=now == run["duration_ticks"])
            for expectation in expectations:
                if expectation["at_tick"] == now:
                    checkpoint(expectation["state"], states[PHYSICAL], states[PROTOCOL], q)
                    record["checkpoints"].append(
                        {"tick": now, "time_us": states[PHYSICAL]["time_us"],
                         "expected": expectation["state"], "status": "pass"})
            for action in actions:
                if action["at_tick"] != now + 1:
                    continue
                kind = action["kind"]
                if kind == "submit":
                    device = PROTOCOL if action["signal"].startswith("payment.") else PHYSICAL
                    command_id += 1
                    ack = json.loads(self.command(
                        f'{device} Submit 1 {streams[device].run} {command_id} '
                        f'{action["at_tick"]} "{action["signal"]}" {action["value"]}'))
                    if not ack.get("accepted"):
                        raise HarnessFailure(f"typed action rejected: {ack}")
                elif kind in ("camera", "payment"):
                    self.configure(kind, action["config"])
                else:
                    # Select a *retained actual charge*, never invent a firmware UUID.
                    charges = [e for e in q if e["signal"] == "charge_centavos"]
                    requests = frames(q, "payment", "tx_byte", -1, states[PROTOCOL]["time_us"])
                    uuids = charge_uuids(requests)
                    if action["charge"] > len(uuids) or not charges:
                        raise HarnessFailure("replay references a charge not yet observed")
                    uuid = uuids[action["charge"] - 1]
                    self.command(f'{PROTOCOL} ReplayPayment "{uuid}" {action["delay_ticks"]}')
        return states
