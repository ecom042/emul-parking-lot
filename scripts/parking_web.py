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

from scenario import driver, parser, runner
from scenario.assessment import FirmwareFailure, HarnessFailure

# The interactive lot: unscheduled, both lanes open, driven by the car queue.
LOT = "lot"
CAR_OPS = {"car.add": {"profile"}, "car.update": {"car", "profile"},
           "car.remove": {"car"}, "car.retry": {"car", "payment"}}


def built_config(boot):
    """Read capacity/tariff from the trusted build next to boot.resc."""
    config = Path(boot).parent / "zephyr/.config"
    values = {"capacity": 4, "tariff_unit_ms": 60000}
    keys = {"CONFIG_PARKING_CAPACITY": "capacity",
            "CONFIG_PARKING_TARIFF_UNIT_MS": "tariff_unit_ms"}
    if not config.is_file():
        raise HarnessFailure(f"missing trusted build configuration {config}")
    for line in config.read_text().splitlines():
        key, _, value = line.partition("=")
        if key in keys:
            values[keys[key]] = int(value)
    return values


class ParkingEngine:
    def __init__(self, runtime, scenarios, evidence, *, pace=0.05, step_ticks=5,
                 capacity=4, tariff_unit_ms=60000):
        parser.integer(step_ticks, 1, 100)
        parser.integer(capacity, 1, 8)
        self.runtime = runtime
        self.pace = pace
        self.step_ticks = step_ticks
        self.capacity = capacity
        self.tariff_unit_ms = tariff_unit_ms
        self.scenarios = {p.stem: parser.load(p) for p in sorted(Path(scenarios).glob("*.json"))}
        self.evidence = Path(evidence)
        self.evidence.mkdir(parents=True, exist_ok=False)
        self.requests = queue.Queue(maxsize=32)
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.publish = lambda snapshot: None
        self.view = {}
        self.commands = collections.deque(maxlen=128)
        self.running = False
        self.selected = LOT
        self.scenario = None
        self.run_index = 0
        self.failure = None
        self.finished = False
        self.revision = 0
        self.run = None
        self.driver = driver.Driver(capacity=capacity)
        self._new_epoch()
        self.state = self.runtime.request({"op": "init"})
        self._configure_run()
        self._refresh()
        self.thread = threading.Thread(target=self._loop, daemon=True)
        self.thread.start()

    def _new_epoch(self):
        self.epoch = uuid.uuid4().hex
        self.ids = set()

    @property
    def tick(self):
        return self.run.tick

    def _spec(self):
        if self.scenario:
            return self.scenario["runs"][self.run_index]
        return runner.open_run()

    def _configure_run(self):
        self.failure = None
        self.finished = False
        data = self.scenario or {}
        self.run = runner.RunController(
            self.runtime.request, self._spec(), self.state,
            seed=data.get("seed", 1), capacity=self.capacity,
            motor_ms=data.get("deadlines", {}).get("motor_ms", 2500),
            step_ticks=self.step_ticks,
            evidence=self.evidence / f"{self.epoch}-{self.run_index}.events.jsonl.gz",
            required_capacity=data.get("capacity"),
            driver=None if self.scenario else self.driver)
        self.state = self.run.state

    def snapshot(self):
        with self.lock:
            return copy.deepcopy(self.view)

    def command(self, payload):
        if self.stop.is_set():
            raise RuntimeError("engine is closing")
        # Validate before admission; no path, code, monitor syntax or arbitrary
        # model method can pass this domain boundary.
        parser.fields(payload, ["v", "run", "id", "op"],
                      ["scenario", "lane", "signal", "value", "car", "profile", "payment"])
        parser.integer(payload["v"], 1, 1)
        parser.require(type(payload["run"]) is str and len(payload["run"]) == 32, "invalid run")
        parser.require(type(payload["id"]) is str and len(payload["id"]) <= 64
                       and payload["id"].isascii() and payload["id"].isalnum(), "invalid command id")
        op = payload["op"]
        parser.require(type(op) is str, "invalid operation")
        allowed = {"start": set(), "pause": set(), "reset": set(),
                   "select": {"scenario"}, "submit": {"lane", "signal", "value"}, **CAR_OPS}
        parser.require(op in allowed, "unknown operation")
        parser.require(set(payload) == {"v", "run", "id", "op"} | allowed[op],
                       "unexpected operation fields")
        if op == "select":
            parser.require(type(payload["scenario"]) is str and
                           payload["scenario"] in {LOT, *self.scenarios}, "unknown scenario")
        if op == "submit":
            parser.require(payload["lane"] in parser.LANES, "unknown lane")
            signal = payload["signal"]
            parser.require(type(signal) is str and signal in parser.SIGNALS, "unknown signal")
            parser.lane_signal(payload["lane"], signal)
            low, high, step = parser.SIGNALS[signal]
            parser.integer(payload["value"], low, high)
            parser.require(payload["value"] % step == 0, "invalid signal step")
        if "car" in payload:
            parser.integer(payload["car"], 1, 1000000)
        if "profile" in payload:
            driver.validate_profile(payload["profile"])
        if "payment" in payload:
            parser.require(payload["payment"] in driver.PROFILE["payment"], "invalid payment")
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

    def _reset(self, selection=None):
        self.running = False
        self._retain_run("reset")
        if selection is not None:
            self.selected = selection
            self.scenario = self.scenarios.get(selection)
        self.commands.clear()
        self._new_epoch()
        self.driver = self.driver.restart()
        self.state = runner.reset(self.runtime.request, self.state)
        self.run_index = 0
        self._configure_run()

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
        elif op in CAR_OPS:
            if self.scenario:
                raise ValueError("the car queue is only available in the lot")
            if op == "car.add":
                return self.driver.add(command["profile"]).view(self.tick)
            if op == "car.update":
                return self.driver.update(command["car"], command["profile"]).view(self.tick)
            if op == "car.remove":
                self.driver.remove(command["car"])
            else:
                return self.driver.retry(command["car"], command["payment"]).view(self.tick)
        else:
            if self.finished or self.failure:
                raise ValueError("reset required after completion or failure")
            return self.run.submit(command["lane"], command["signal"], command["value"])
        return {"accepted": True, "run": self.epoch, "op": op}

    def _advance(self):
        final = self.run.advance()
        self.state = self.run.state
        if not final:
            return
        self._retain_run("pass")
        if self.scenario and self.run_index + 1 < len(self.scenario["runs"]):
            self.state = runner.reset(self.runtime.request, self.state)
            self._new_epoch()
            self.run_index += 1
            self._configure_run()
        else:
            self.finished = True
            self.running = False

    def _retain_run(self, status):
        if not self.run or self.run.log is None:
            return
        run = self.run
        record = {"v": 2, "status": status, "scenario": self.selected,
                  "run": self.epoch, "index": self.run_index, "failure": self.failure,
                  "capacity": self.capacity, "tariff_unit_ms": self.tariff_unit_ms,
                  "state": {k: v for k, v in run.state.items() if k not in ("events", "pixels")},
                  "events_file": run.log.path.name,
                  "event_counts": {o: s.count for o, s in run.streams.items()},
                  "sessions": sorted(run.lot.sessions), "cycles": {
                      lane: list(m.cycles) for lane, m in run.monitors.items()},
                  "commands": list(self.commands), "equipment": list(run.commands),
                  "results": list(run.results),
                  "queue": None if self.scenario else self.driver.view()}
        run.close()
        name = f"{self.epoch}-{self.run_index}-{status}.json"
        (self.evidence / name).write_text(json.dumps(record) + "\n")

    def _refresh(self):
        self.revision += 1
        run = self.run
        state = run.state
        view = {"v": 2, "revision": self.revision, "run": self.epoch, "running": self.running,
                "scenario": self.selected, "scenarios": [LOT, *self.scenarios],
                "run_index": self.run_index, "finished": self.finished,
                "failure": self.failure, "lanes": state["lanes"],
                "display": state.get("display"), "pixels": state.get("pixels"),
                "lot": {"capacity": self.capacity, "tariff_unit_ms": self.tariff_unit_ms,
                        "sessions": sorted(run.lot.sessions)},
                "counts": {lane: dict(m.counts) for lane, m in run.monitors.items()},
                "commands": list(self.commands), "equipment": list(run.commands),
                "results": list(run.results), "events": run.recent_events(),
                "queue": None if self.scenario else self.driver.view()}
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
