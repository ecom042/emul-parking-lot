"""Behavioral checks over source-time traces, never firmware logs or symbols.

Checks run incrementally so a run can last arbitrarily long: each lane keeps
only the state its oracles need (the current authorization cycle, a few
counters and fold state), while the complete trace goes to an on-disk log.
`assess` keeps the original whole-list interface for host tests.
"""

import collections
import gzip
import json

from scenario.obstruction import ObstructionMonitor
from scenario.observations import assert_green_cycle, charge_uuids


class FirmwareFailure(AssertionError):
    pass


class HarnessFailure(RuntimeError):
    pass


class LaneMonitor:
    """Continuous safety and every-green authorization for one lane.

    `windows` is a scheduled fixture's ordered green windows; None accepts any
    number of greens (manual and car-queue runs), each still fully authorized.
    Exit-lane greens require a matching paid charge unless a v1-style window
    says otherwise. Ten milliseconds is the documented controller
    servicing/tick tolerance, not time granted per harvest or reversal.
    """

    def __init__(self, lane="entry", windows=None, motor_ms=2500, payment_required=None):
        self.lane = lane
        self.windows = windows
        self.motor_ms = motor_ms
        self.payment_required = lane == "exit" if payment_required is None else payment_required
        self.obstruction = ObstructionMonitor()
        self.enable_start = None
        self.commanded = []
        self.positions = collections.deque(maxlen=1024)
        self.protocol = []
        self.beam_value = None
        self.beam = collections.deque(maxlen=64)
        self.greens = 0
        self.counts = {"green": 0, "buzzer": 0}
        self.cycles = collections.deque(maxlen=256)
        self.open_cycle = None
        self.charges = []
        self.payment_bytes = []
        self.last_time_us = 0

    def feed(self, physical, protocol, time_us):
        """Consume one harvest of new events; return lot events, raise on violation."""
        for event in physical:
            self._physical(event)
        error = self.obstruction.finish(time_us)
        if error:
            raise FirmwareFailure(error)
        limit = (self.motor_ms + 10) * 1000
        if self.enable_start is not None and time_us - self.enable_start > limit:
            raise FirmwareFailure(f"motor_deadline: enabled since {self.enable_start} us, "
                                  f"still on at {time_us} us")
        lot = []
        for event in protocol:
            cycle = self._protocol(event)
            if cycle:
                lot.append((cycle["green_us"], 1, self.lane, "green", cycle))
        passage = self._passage()
        if passage:
            lot.append((passage["passage_us"], 0, self.lane, "passage", passage))
        self.last_time_us = time_us
        return lot

    def finish(self, final):
        if final and self.windows is not None and self.greens != len(self.windows):
            raise FirmwareFailure(f"green_count: expected {len(self.windows)}, "
                                  f"observed {self.greens}")

    def _physical(self, event):
        error = self.obstruction.feed(event)
        if error:
            raise FirmwareFailure(error)
        device, signal = event["device"], event["signal"]
        if device == "motor" and signal == "enable":
            if event["value"]:
                if self.enable_start is None:
                    self.enable_start = event["time_us"]
            elif self.enable_start is not None:
                if event["time_us"] - self.enable_start > (self.motor_ms + 10) * 1000:
                    raise FirmwareFailure(f"motor_deadline: enabled from {self.enable_start} "
                                          f'to {event["time_us"]} us')
                self.enable_start = None
        if event["category"] == "commanded" and device == "motor":
            self.commanded.append(event)
        elif device == "barrier" and signal == "position_mdeg":
            self.positions.append(event)
        elif device == "beam" and signal == "obstruction" and event["value"] != self.beam_value:
            self.beam_value = event["value"]
            self.beam.append(event)

    def _protocol(self, event):
        self.protocol.append(event)
        if len(self.protocol) > 500000:
            raise HarnessFailure("authorization window exceeded its evidence bound")
        signal, value = event["signal"], event["value"]
        if event["device"] == "payment" and signal == "tx_byte":
            self.payment_bytes.append(chr(value))
            if value == 10:
                frame = "".join(self.payment_bytes[:-1])
                self.payment_bytes = []
                for uuid in charge_uuids([frame]):
                    if uuid not in self.charges:
                        self.charges.append(uuid)
        if signal in self.counts and value == 1:
            self.counts[signal] += 1
        if signal != "green":
            return None
        if value == 0:
            self._trim(event)
            return None
        index = self.greens
        self.greens += 1
        window = None
        if self.windows is not None:
            if index >= len(self.windows):
                raise FirmwareFailure(f'unexpected_green: at {event["time_us"]} us')
            window = self.windows[index]
            if not window["after_tick"] < event["tick"] <= window["before_tick"]:
                raise FirmwareFailure(f'green_window: tick {event["tick"]}, expected {window}')
        required = (window or {}).get("payment_required", self.payment_required)
        physical = sorted([*self.commanded, *self.positions], key=lambda e: e["seq"])
        try:
            cycle = assert_green_cycle(
                [e for e in physical if e["time_us"] <= event["time_us"]],
                self.protocol, required)
        except (AssertionError, IndexError) as error:
            raise FirmwareFailure(f'green_authorization: at {event["time_us"]} us: '
                                  f"{error}") from error
        cycle["lane"] = self.lane
        self.cycles.append(cycle)
        self.open_cycle = cycle
        return cycle

    def _trim(self, green_off):
        """A green-off closes the cycle; older frames can never authorize again."""
        self.protocol = [green_off]
        at = green_off["time_us"]
        directions = [e for e in self.commanded
                      if e["signal"] == "direction" and e["time_us"] < at]
        self.commanded = directions[-1:] + [e for e in self.commanded if e["time_us"] >= at]

    def _passage(self):
        """Completed passage: beam active, then clear, after this cycle's green."""
        cycle = self.open_cycle
        if cycle is None:
            return None
        active = None
        for event in self.beam:
            if event["time_us"] <= cycle["green_us"]:
                continue
            if event["value"] and active is None:
                active = event
            elif not event["value"] and active is not None:
                cycle["passage_us"] = event["time_us"]
                self.open_cycle = None
                return cycle
        return None


class LotMonitor:
    """Sessions and capacity observed across both lanes.

    Only a completed entry passage parks a plate; only a completed paid exit
    passage removes it. Greens are checked against that observed lot.
    """

    def __init__(self, capacity):
        self.capacity = capacity
        self.sessions = {}

    def apply(self, events):
        for at, _, lane, kind, cycle in sorted(events, key=lambda e: e[:2]):
            plate = cycle["plate"]
            if kind == "green" and lane == "entry":
                if plate in self.sessions:
                    raise FirmwareFailure(f"duplicate_entry: {plate} already parked, "
                                          f"entry green at {at} us")
                if len(self.sessions) >= self.capacity:
                    raise FirmwareFailure(f"capacity_exceeded: {len(self.sessions)} of "
                                          f"{self.capacity} spots used, entry green at {at} us")
            elif kind == "green":
                if plate not in self.sessions:
                    raise FirmwareFailure(f"exit_without_session: {plate} has no session, "
                                          f"exit green at {at} us")
            elif lane == "entry":
                self.sessions[plate] = at
            else:
                self.sessions.pop(plate, None)


def assess(physical, protocol, snapshot, windows, motor_ms=2500, final=False):
    """Check *all* enabled intervals and *every* green transition in complete lists."""
    monitor = LaneMonitor(windows=windows, motor_ms=motor_ms, payment_required=False)
    monitor.feed(physical, protocol, snapshot["time_us"])
    monitor.finish(final)


def checkpoint(expected, physical, protocol, trace=None, counts=None):
    """Compare actual state; counts come from a monitor or a complete trace."""
    actual = {**physical, **protocol}
    for signal in ("green", "buzzer"):
        actual[signal + "_count"] = counts[signal] if counts is not None else sum(
            e["signal"] == signal and e["value"] == 1 for e in trace or [])
    for key, value in expected.items():
        if key in ("position_min_mdeg", "position_max_mdeg"):
            position = physical["position_mdeg"]
            valid = position >= value if key == "position_min_mdeg" else position <= value
            if not valid:
                raise FirmwareFailure(f'checkpoint at {physical["time_us"]} us: '
                                      f'{key} {value}, observed position {position}')
            continue
        if actual.get(key) != value:
            raise FirmwareFailure(
                f'checkpoint at {physical["time_us"]} us: {key} expected {value!r}, '
                f'observed {actual.get(key)!r}')


class EvidenceLog:
    """Append-only compressed JSONL of every harvested source event."""

    def __init__(self, path, quota_bytes=512 * 1024 * 1024):
        self.path = path
        self.quota = quota_bytes
        self.written = 0
        self.handle = gzip.open(path, "at", encoding="utf-8")

    def write(self, owner, event):
        line = json.dumps({"owner": owner, **event}, separators=(",", ":")) + "\n"
        self.written += len(line)
        if self.written > self.quota:
            raise HarnessFailure("source evidence quota exceeded; reset to continue")
        self.handle.write(line)

    def close(self):
        if self.handle:
            self.handle.close()
            self.handle = None


class Stream:
    """Deduplicate a bounded ring, rejecting silent gaps and old-run events.

    Memory holds only a recent window; `sink` receives every new event.
    """

    def __init__(self, run, minimum_time, sink=None, window=4096):
        self.run = run
        self.minimum_time = minimum_time
        self.sequence = 0
        self.count = 0
        self.sink = sink
        self.recent = collections.deque(maxlen=window)

    @property
    def events(self):
        return list(self.recent)

    def collect(self, events):
        new = []
        for event in events:
            if event["run"] != self.run or event["time_us"] < self.minimum_time:
                raise HarnessFailure("stale source event/run after reset")
            if event["seq"] <= self.sequence:
                continue
            if event["seq"] != self.sequence + 1:
                raise HarnessFailure("source trace gap; assessment cannot be trusted")
            if self.recent and event["time_us"] < self.recent[-1]["time_us"]:
                raise HarnessFailure("source timestamps moved backwards")
            self.recent.append(event)
            self.sequence = event["seq"]
            self.count += 1
            new.append(event)
            if self.sink:
                self.sink(event)
        return new
