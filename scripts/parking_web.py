"""Parking domain adapter; one worker owns the finite virtual-time control context."""
import collections
import concurrent.futures
import copy
import json
from pathlib import Path
import queue
import threading
import time
import uuid

from scenario import parser
from scenario.assessment import FirmwareFailure, HarnessFailure, Stream, assess, checkpoint
from scenario.observations import charge_uuids, frames


class ParkingEngine:
    def __init__(self, runtime, scenarios, evidence, *, pace=0.05, step_ticks=5):
        parser.integer(step_ticks, 1, 100)
        self.runtime = runtime
        self.pace = pace
        self.step_ticks = step_ticks
        self.scenarios = {p.stem: parser.load(p) for p in sorted(Path(scenarios).glob("*.json"))}
        self.evidence = Path(evidence)
        self.evidence.mkdir(parents=True, exist_ok=False)
        self.requests = queue.Queue(maxsize=32)
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.publish = lambda snapshot: None
        self.view = {}
        self.state = None
        self.commands = collections.deque(maxlen=128)
        self.results = collections.deque(maxlen=128)
        self.running = False
        self.selected = "manual"
        self.scenario = None
        self.run_index = 0
        self.streams = {}
        self.failure = None
        self.command_id = 0
        self.revision = 0
        self._new_epoch()
        self._rpc({"op": "init"})
        self._configure_run()
        self._refresh()
        self.thread = threading.Thread(target=self._loop, daemon=True)
        self.thread.start()

    def _new_epoch(self):
        self.epoch = uuid.uuid4().hex
        self.ids = set()

    def _rpc(self, payload):
        result = self.runtime.request(payload)
        if "physical" in result:
            if "pixels" not in result and self.state:
                result["pixels"] = self.state["pixels"]
            self.state = result
            for owner in ("physical", "protocol"):
                source = result[owner]
                if owner not in self.streams:
                    self.streams[owner] = Stream(source["run"], source["time_us"])
                elif self.streams[owner].run != source["run"]:
                    raise HarnessFailure("unexpected source run change")
                self.streams[owner].collect(result["events"][owner])
        return result

    @property
    def tick(self):
        return self.state["physical"]["tick"]

    def _configure_run(self):
        if any(self.state[o]["tick"] != 0 or self.state[o]["pending"]
               for o in ("physical", "protocol")):
            raise HarnessFailure("run configuration requires empty paused Tick 0")
        self.processed = set()
        self.checked = set()
        self.finished = False
        self.failure = None
        self.command_id = 0
        self.scheduled_ids = set()
        self.run_spec = self.scenario["runs"][self.run_index] if self.scenario else {
            "initial": {"position_mdeg": 0, "vehicle_mm": 6000},
            "duration_ticks": 6000, "actions": [], "expect": [],
            "green": [{"after_tick": 0, "before_tick": 6000,
                       "payment_required": bool(i % 2)} for i in range(32)]}
        initial = self.run_spec["initial"]
        self._rpc({"op": "initial", **initial,
                   "seed": self.scenario["seed"] if self.scenario else 1})
        for kind in ("camera", "payment"):
            if kind in self.run_spec:
                self._rpc({"op": "configure", "kind": kind,
                           "config": self.run_spec[kind]})
        if not self.scenario:
            self._rpc({"op": "configure", "kind": "payment", "config": {
                "delay_ticks": 10, "fragment_bytes": 8, "result": "PAID", "missing": True}})

    def snapshot(self):
        with self.lock:
            return copy.deepcopy(self.view)

    def command(self, payload):
        if self.stop.is_set():
            raise RuntimeError("engine is closing")
        # Validate before admission; no path, code, monitor syntax or arbitrary
        # model method can pass this domain boundary.
        parser.fields(payload, ["v", "run", "id", "op"], ["scenario", "signal", "value"])
        parser.integer(payload["v"], 1, 1)
        parser.require(type(payload["run"]) is str and len(payload["run"]) == 32, "invalid run")
        parser.require(type(payload["id"]) is str and len(payload["id"]) <= 64
                       and payload["id"].isascii() and payload["id"].isalnum(), "invalid command id")
        op = payload["op"]
        parser.require(type(op) is str, "invalid operation")
        allowed = {"start": set(), "pause": set(), "reset": set(),
                   "select": {"scenario"}, "submit": {"signal", "value"}}
        parser.require(op in allowed, "unknown operation")
        parser.require(set(payload) == {"v", "run", "id", "op"} | allowed[op],
                       "unexpected operation fields")
        if op == "select":
            parser.require(type(payload["scenario"]) is str and
                           payload["scenario"] in {"manual", *self.scenarios}, "unknown scenario")
        if op == "submit":
            signal = payload["signal"]
            parser.require(type(signal) is str and signal in parser.SIGNALS, "unknown signal")
            low, high, step = parser.SIGNALS[signal]
            parser.integer(payload["value"], low, high)
            parser.require(payload["value"] % step == 0, "invalid signal step")
        future = concurrent.futures.Future()
        try:
            self.requests.put_nowait((copy.deepcopy(payload), future))
        except queue.Full as error:
            raise ValueError("command queue full") from error
        # Deadline removes not-yet-started commands, avoiding uncertain later
        # execution after an HTTP timeout. Running commands return their result.
        try:
            return future.result(timeout=25)
        except concurrent.futures.TimeoutError:
            if future.cancel():
                raise RuntimeError("command expired before execution")
            raise RuntimeError("runtime command outcome uncertain; inspect snapshot")

    def _submit(self, signal, value, at_tick, *, scheduled=False):
        owner = "protocol" if signal.startswith("payment.") else "physical"
        self.command_id += 1
        if scheduled:
            self.scheduled_ids.add(self.command_id)
        command = {"v": 1, "run": self.state[owner]["run"], "id": self.command_id,
                   "at_tick": at_tick, "signal": signal, "value": value}
        ack = self._rpc({"op": "submit", "owner": owner, "command": command})
        if not ack.get("accepted"):
            raise RuntimeError(f"equipment rejected command: {ack}")
        return ack

    def _reset(self, selection=None):
        self.running = False
        self._retain_run("reset")
        if selection is not None:
            self.selected = selection
            self.scenario = self.scenarios.get(selection)
        self.results.clear()
        self.commands.clear()
        self._new_epoch()
        self._reset_equipment()
        self.run_index = 0
        self._configure_run()

    def _reset_equipment(self):
        previous = self.state
        self.streams = {}
        self._rpc({"op": "reset"})
        for owner in ("physical", "protocol"):
            if self.state[owner]["run"] == previous[owner]["run"]:
                raise HarnessFailure("reset retained source run identity")
            if self.state[owner]["time_us"] < previous[owner]["time_us"]:
                raise HarnessFailure("reset moved source time backwards")
            signal = "sensor.noise" if owner == "physical" else "payment.replay"
            ack = self._rpc({"op": "submit", "owner": owner, "command": {
                "v": 1, "run": previous[owner]["run"], "id": 2147483647,
                "at_tick": 1, "signal": signal, "value": 0 if owner == "physical" else 1}})
            if ack.get("accepted") is not False or ack.get("error") != "stale_run":
                raise HarnessFailure("old source run command did not reject")

    def _execute(self, command):
        if command["run"] != self.epoch:
            raise ValueError("stale_run: reconnect for a current snapshot")
        if command["id"] in self.ids:
            raise ValueError("duplicate command id; inspect previous result")
        op = command["op"]
        if len(self.ids) >= 4096 and op not in ("reset", "select"):
            raise ValueError("command capacity reached; reset required")
        self.ids.add(command["id"])
        if op == "start":
            if self.finished or self.failure:
                raise ValueError("reset required after completion or failure")
            self.running = True
        elif op == "pause":
            self.running = False
        elif op == "reset":
            self._reset()
        elif op == "select":
            self._reset(selection=command["scenario"])
        else:
            if self.finished or self.failure:
                raise ValueError("reset required after completion or failure")
            return self._submit(command["signal"], command["value"], self.tick + 1)
        return {"accepted": True, "run": self.epoch, "op": op}

    def _scheduled(self):
        for index, action in enumerate(self.run_spec["actions"]):
            if index in self.processed or action["at_tick"] != self.tick + 1:
                continue
            self.processed.add(index)
            if action["kind"] == "submit":
                self._submit(action["signal"], action["value"], action["at_tick"], scheduled=True)
            elif action["kind"] in ("camera", "payment"):
                self._rpc({"op": "configure", "kind": action["kind"], "config": action["config"]})
            else:
                q = self.streams["protocol"].events
                uuids = charge_uuids(frames(q, "payment", "tx_byte", -1,
                                           self.state["protocol"]["time_us"]))
                if action["charge"] > len(uuids):
                    raise RuntimeError("replay references unobserved charge")
                self._rpc({"op": "replay", "uuid": uuids[action["charge"] - 1],
                           "delay_ticks": action["delay_ticks"]})

    def _advance(self):
        self._scheduled()
        boundaries = [self.run_spec["duration_ticks"], self.tick + self.step_ticks]
        boundaries += [a["at_tick"] - 1 for i, a in enumerate(self.run_spec["actions"])
                       if i not in self.processed and a["at_tick"] - 1 > self.tick]
        boundaries += [e["at_tick"] for i, e in enumerate(self.run_spec["expect"])
                       if i not in self.checked and e["at_tick"] > self.tick]
        target = min(boundaries)
        self._rpc({"op": "step", "ticks": target - self.tick})
        if any(self.state[o]["tick"] != target for o in ("physical", "protocol")):
            raise HarnessFailure("finite step did not reach exact source tick")
        p, q = (self.streams[o].events for o in ("physical", "protocol"))
        for event in self.state["events"]["physical"] + self.state["events"]["protocol"]:
            if event["category"] == "command" and event["signal"] == "failed":
                self.commands.append({"status": "failed", "event": event})
                if event["value"] in self.scheduled_ids:
                    raise HarnessFailure("scheduled scenario equipment command failed")
        final = self.tick == self.run_spec["duration_ticks"]
        assess(p, q, self.state["physical"], self.run_spec["green"],
               final=final and self.scenario is not None)
        for index, expectation in enumerate(self.run_spec["expect"]):
            if index not in self.checked and expectation["at_tick"] == self.tick:
                checkpoint(expectation["state"], self.state["physical"], self.state["protocol"], q)
                self.checked.add(index)
                self.results.append({"status": "pass", "run_index": self.run_index,
                                     "tick": self.tick, "expected": expectation["state"]})
        if final:
            self._retain_run("pass")
            if self.scenario and self.run_index + 1 < len(self.scenario["runs"]):
                self._reset_equipment()
                self._new_epoch()
                self.run_index += 1
                self._configure_run()
            else:
                self.finished = True
                self.running = False

    def _retain_run(self, status):
        if not self.state:
            return
        record = {"v": 1, "status": status, "scenario": self.selected,
                  "run": self.epoch, "index": self.run_index, "failure": self.failure,
                  "state": {k: v for k, v in self.state.items() if k != "events"},
                  "events": {o: s.events for o, s in self.streams.items()},
                  "commands": list(self.commands), "results": list(self.results)}
        name = f"{self.epoch}-{self.run_index}-{status}.json"
        (self.evidence / name).write_text(json.dumps(record) + "\n")

    def _refresh(self):
        self.revision += 1
        source = {k: v for k, v in self.state.items() if k != "events"}
        view = {"v": 1, "revision": self.revision, "run": self.epoch, "running": self.running,
                "scenario": self.selected, "scenarios": ["manual", *self.scenarios],
                "run_index": self.run_index, "finished": self.finished,
                "failure": self.failure, **source,
                "commands": list(self.commands), "results": list(self.results),
                "events": {o: s.events[-128:] for o, s in self.streams.items()}}
        with self.lock:
            self.view = view
        self.publish(view)

    def _loop(self):
        while not self.stop.is_set():
            began = time.monotonic()
            if self.runtime.proc.poll() is not None and not self.failure:
                self.failure = {"kind": "harness_failure",
                                "message": "Renode process exited; restart the local session"}
                self.running = False
                self._retain_run("failure")
                self._refresh()
            try:
                item = self.requests.get(timeout=0.001 if self.running else 0.1)
            except queue.Empty:
                item = None
            if item:
                command, future = item
                if future.set_running_or_notify_cancel():
                    try:
                        result = self._execute(command)
                        self.commands.append({"id": command["id"], "op": command["op"],
                                              "status": "accepted", "result": result})
                        self._refresh()
                        future.set_result(result)
                    except Exception as error:
                        self.commands.append({"id": command["id"], "status": "rejected",
                                              "error": str(error)})
                        if not isinstance(error, ValueError):
                            self.failure = {"kind": "harness_failure", "message": str(error)}
                            self.running = False
                            self._retain_run("failure")
                        self._refresh()
                        future.set_exception(error)
            if self.running:
                try:
                    self._advance()
                except Exception as error:
                    self.failure = {"kind": "firmware_failure" if isinstance(error, FirmwareFailure)
                                    else "harness_failure", "message": str(error)}
                    self.running = False
                    self._retain_run("failure")
                self._refresh()
            # Only rendering pace is wall-clock based. Every firmware/model
            # deadline above remains in the shared Renode virtual-time domain.
            self.stop.wait(max(0, self.pace - (time.monotonic() - began)))

    def close(self):
        self.stop.set()
        while True:
            try:
                _, future = self.requests.get_nowait()
                future.cancel()
            except queue.Empty:
                break
        self.thread.join(timeout=25)
        self.runtime.close()
        self._retain_run("closed")
