"""Behavioral checks over source-time traces, never firmware logs or symbols."""

from scenario.obstruction import obstruction_violation
from scenario.observations import assert_green_cycle


class FirmwareFailure(AssertionError):
    pass


class HarnessFailure(RuntimeError):
    pass


def assess(physical, protocol, snapshot, windows, motor_ms=2500, final=False):
    """Check *all* enabled intervals and *every* green transition in this run.

    Ten milliseconds is the documented controller servicing/tick tolerance,
    not additional time granted per harvest, reversal, endpoint or jam.
    """
    obstruction_error = obstruction_violation(physical, snapshot)
    if obstruction_error:
        raise FirmwareFailure(obstruction_error)
    start = None
    for event in physical:
        if event["device"] == "motor" and event["signal"] == "enable":
            if event["value"]:
                if start is None:
                    start = event["time_us"]
            elif start is not None:
                if event["time_us"] - start > (motor_ms + 10) * 1000:
                    raise FirmwareFailure(f"motor_deadline: enabled from {start} to "
                                          f'{event["time_us"]} us')
                start = None
    if start is not None and snapshot["time_us"] - start > (motor_ms + 10) * 1000:
        raise FirmwareFailure(f'motor_deadline: enabled since {start} us, still on at '
                              f'{snapshot["time_us"]} us')
    greens = [e for e in protocol if e["signal"] == "green" and e["value"] == 1]
    for index, green in enumerate(greens):
        if index >= len(windows):
            raise FirmwareFailure(f'unexpected_green: at {green["time_us"]} us')
        window = windows[index]
        if not window["after_tick"] < green["tick"] <= window["before_tick"]:
            raise FirmwareFailure(f'green_window: tick {green["tick"]}, expected {window}')
        # Prefix by event index rather than timestamp so equal-time later GPIO
        # transitions cannot hide an earlier bad green on the same timeline.
        boundary = next(i for i, e in enumerate(protocol) if e is green)
        try:
            assert_green_cycle(
                [e for e in physical if e["time_us"] <= green["time_us"]],
                protocol[:boundary + 1], window["payment_required"])
        except (AssertionError, IndexError) as error:
            raise FirmwareFailure(f'green_authorization: at {green["time_us"]} us: {error}') from error
    if final and len(greens) != len(windows):
        raise FirmwareFailure(f"green_count: expected {len(windows)}, observed {len(greens)}")


def checkpoint(expected, physical, protocol, trace):
    actual = {**physical, **protocol}
    for signal in ("green", "buzzer"):
        actual[signal + "_count"] = sum(
            e["signal"] == signal and e["value"] == 1 for e in trace)
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


class Stream:
    """Deduplicate a bounded ring, rejecting silent gaps and old-run events."""

    def __init__(self, run, minimum_time):
        self.run = run
        self.minimum_time = minimum_time
        self.sequence = 0
        self.events = []

    def collect(self, events):
        for event in events:
            if event["run"] != self.run or event["time_us"] < self.minimum_time:
                raise HarnessFailure("stale source event/run after reset")
            if event["seq"] <= self.sequence:
                continue
            if event["seq"] != self.sequence + 1:
                raise HarnessFailure("source trace gap; assessment cannot be trusted")
            if self.events and event["time_us"] < self.events[-1]["time_us"]:
                raise HarnessFailure("source timestamps moved backwards")
            self.events.append(event)
            self.sequence = event["seq"]
            if len(self.events) > 250000:
                raise HarnessFailure("source evidence capacity exceeded")
