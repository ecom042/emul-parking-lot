"""Closing safety from the physical owner's ordered GPIO/sensor observations."""


class ObstructionMonitor:
    """Incremental fold over one lane's physical sequence.

    Position, enable, direction and obstruction share one source sequence.
    A closing obstruction at a partial position requires intervention/reset;
    clearing the beam alone cannot authorize a reversal or another drive.
    The state is a handful of values, so arbitrarily long runs need no history.
    """

    def __init__(self, stop_budget_us=10000):
        self.stop_budget_us = stop_budget_us
        self.enabled = self.opening = self.blocked = False
        self.position = 0
        self.stop_due = None
        self.latched = False

    def feed(self, event):
        """Return the first violation caused by this event, or None."""
        device, signal, value = event["device"], event["signal"], event["value"]
        at = event["time_us"]
        if self.stop_due is not None and at > self.stop_due:
            return f"obstruction_stop_deadline: motor still enabled at {at} us"
        if device == "barrier" and signal == "position_mdeg":
            self.position = value
        elif device == "motor" and signal == "direction":
            was_opening = self.opening
            self.opening = bool(value)
            if self.enabled and was_opening and not self.opening and self.blocked:
                return f"obstruction_before_close: closing direction at {at} us"
        elif device == "motor" and signal == "enable":
            self.enabled = bool(value)
            if self.enabled:
                if self.latched:
                    return f"obstruction_reenable: partial fault drive restarted at {at} us"
                if self.blocked and not self.opening:
                    return f"obstruction_before_close: closing motor enabled at {at} us"
            else:
                self.stop_due = None
        elif device == "beam" and signal == "obstruction":
            was_blocked = self.blocked
            self.blocked = bool(value)
            if self.blocked and not was_blocked and self.enabled and not self.opening:
                self.stop_due = at + self.stop_budget_us
                self.latched = self.position > 0
        # Direction reversal without disabling is also a new drive after a
        # partial fault, even if its original enable interval has not ended.
        if self.latched and self.enabled and self.opening:
            return f"obstruction_reversal: partial fault reversed at {at} us"
        return None

    def finish(self, time_us):
        """Check a pending stop deadline against the current source time."""
        if self.stop_due is not None and time_us > self.stop_due:
            return f"obstruction_stop_deadline: motor still enabled at {time_us} us"
        return None


def obstruction_violation(events, snapshot, stop_budget_us=10000):
    """Return the first violation over a complete list, or None."""
    monitor = ObstructionMonitor(stop_budget_us)
    for event in events:
        error = monitor.feed(event)
        if error:
            return error
    return monitor.finish(snapshot["time_us"])
