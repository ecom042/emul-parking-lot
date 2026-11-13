"""Closing safety from the physical owner's ordered GPIO/sensor observations."""


def obstruction_violation(events, snapshot, stop_budget_us=10000):
    """Return the first violation, or None.

    Position, enable, direction and obstruction share one source sequence.
    A closing obstruction at a partial position requires intervention/reset;
    clearing the beam alone cannot authorize a reversal or another drive.
    """
    enabled = opening = blocked = False
    position = 0
    stop_due = None
    latched = False
    for event in events:
        device, signal, value = event["device"], event["signal"], event["value"]
        at = event["time_us"]
        if stop_due is not None and at > stop_due:
            return f"obstruction_stop_deadline: motor still enabled at {at} us"
        if device == "barrier" and signal == "position_mdeg":
            position = value
        elif device == "motor" and signal == "direction":
            was_opening = opening
            opening = bool(value)
            if enabled and was_opening and not opening and blocked:
                return f"obstruction_before_close: closing direction at {at} us"
        elif device == "motor" and signal == "enable":
            enabled = bool(value)
            if enabled:
                if latched:
                    return f"obstruction_reenable: partial fault drive restarted at {at} us"
                if blocked and not opening:
                    return f"obstruction_before_close: closing motor enabled at {at} us"
            else:
                stop_due = None
        elif device == "beam" and signal == "obstruction":
            was_blocked = blocked
            blocked = bool(value)
            if blocked and not was_blocked and enabled and not opening:
                stop_due = at + stop_budget_us
                latched = position > 0
        # Direction reversal without disabling is also a new drive after a
        # partial fault, even if its original enable interval has not ended.
        if latched and enabled and opening:
            return f"obstruction_reversal: partial fault reversed at {at} us"
    if stop_due is not None and snapshot["time_us"] > stop_due:
        return ("obstruction_stop_deadline: motor still enabled at "
                f'{snapshot["time_us"]} us')
    return None
