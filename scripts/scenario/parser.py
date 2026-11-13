"""Strict v1 parser. No scenario string is executable monitor syntax."""

import json
from pathlib import Path
import re

MAX_BYTES = 65536
MAX_DEPTH = 12
MAX_ACTIONS = 256
MAX_TICKS = 12000


class ScenarioError(ValueError):
    """Invalid scenario, before any simulator is launched."""


def require(condition, message):
    if not condition:
        raise ScenarioError(message)


def integer(value, low, high):
    require(type(value) is int and low <= value <= high,
            f"expected integer {low}..{high}, got {value!r}")


def fields(value, required, optional=()):
    require(type(value) is dict, "expected object")
    require(set(required) <= value.keys(), f"missing fields: {set(required) - value.keys()}")
    require(value.keys() <= set(required) | set(optional),
            f"unknown fields: {value.keys() - set(required) - set(optional)}")


def boolean(value):
    require(type(value) is bool, "expected boolean")


def camera(value):
    fields(value, ["plate", "confidences", "delay_ticks", "fragment_bytes", "missing"])
    require(type(value["plate"]) is str and
            re.fullmatch(r"[A-Z]{3}[0-9][A-Z0-9][0-9]{2}", value["plate"]),
            "invalid camera plate")
    require(type(value["confidences"]) is list and 1 <= len(value["confidences"]) <= 16,
            "expected 1..16 confidences")
    for confidence in value["confidences"]:
        integer(confidence, 0, 100)
    timing(value)


def timing(value):
    integer(value["delay_ticks"], 1, 3000)
    integer(value["fragment_bytes"], 1, 128)
    boolean(value["missing"])


def payment(value):
    fields(value, ["delay_ticks", "fragment_bytes", "missing", "result"])
    require(value["result"] in ("PAID", "CANCELLED", "MALFORMED"), "invalid payment result")
    timing(value)


SIGNALS = {
    "vehicle.speed": (-2000, 2000, 100),
    "vehicle.next": (1, 1, 1),
    "sensor.obstruction": (0, 1, 1),
    "sensor.noise": (0, 64, 1),
    "sensor.seed": (1, 2147483647, 1),
    "fault.jam": (0, 1, 1),
    "fault.limits": (0, 3, 1),
    "payment.replay": (1, 1, 1),
    "payment.cancel": (1, 1, 1),
}
STATE = {
    "position_mdeg": (0, 90000), "vehicle_mm": (-2000, 6000),
    "position_min_mdeg": (0, 90000), "position_max_mdeg": (0, 90000),
    "captures": (0, 64), "payments": (0, 64), "replies": (0, 1024),
    "pending": (0, 256), "green_count": (0, 32), "buzzer_count": (0, 64),
    "enable": None, "green": None, "red": None, "buzzer": None,
}


def validate(data):
    fields(data, ["version", "name", "seed", "deadlines", "runs"])
    integer(data["version"], 1, 1)
    require(type(data["name"]) is str and
            re.fullmatch(r"[a-z][a-z0-9-]{0,63}", data["name"]), "invalid scenario name")
    integer(data["seed"], 1, 2147483647)
    fields(data["deadlines"], ["motor_ms"])
    integer(data["deadlines"]["motor_ms"], 2500, 2500)
    require(type(data["runs"]) is list and 1 <= len(data["runs"]) <= 8, "expected 1..8 runs")
    total_ticks = total_actions = 0
    for run in data["runs"]:
        fields(run, ["initial", "duration_ticks", "actions", "expect", "green"],
               ["camera", "payment"])
        fields(run["initial"], ["position_mdeg", "vehicle_mm"])
        integer(run["initial"]["position_mdeg"], 0, 90000)
        integer(run["initial"]["vehicle_mm"], -2000, 6000)
        integer(run["duration_ticks"], 1, 6000)
        end = run["duration_ticks"]
        total_ticks += end
        for key, check in (("camera", camera), ("payment", payment)):
            if key in run:
                check(run[key])
        require(type(run["actions"]) is list, "actions must be an array")
        total_actions += len(run["actions"])
        previous = 0
        for action in run["actions"]:
            require(type(action) is dict, "action must be an object")
            kind = action.get("kind")
            if kind == "submit":
                fields(action, ["at_tick", "kind", "signal", "value"])
                require(type(action["signal"]) is str and action["signal"] in SIGNALS,
                        "unknown typed signal")
                low, high, step = SIGNALS[action["signal"]]
                integer(action["value"], low, high)
                require(action["value"] % step == 0, "invalid signal step")
            elif kind in ("camera", "payment"):
                fields(action, ["at_tick", "kind", "config"])
                (camera if kind == "camera" else payment)(action["config"])
            elif kind == "replay":
                fields(action, ["at_tick", "kind", "charge", "delay_ticks"])
                integer(action["charge"], 1, 64)
                integer(action["delay_ticks"], 1, 3000)
            else:
                raise ScenarioError("unknown action kind")
            integer(action["at_tick"], 1, end)
            require(action["at_tick"] >= previous, "actions must be time ordered")
            previous = action["at_tick"]
        require(type(run["expect"]) is list and 1 <= len(run["expect"]) <= 128,
                "expected 1..128 checkpoints")
        previous = 0
        for expectation in run["expect"]:
            fields(expectation, ["at_tick", "state"])
            integer(expectation["at_tick"], 1, end)
            require(expectation["at_tick"] >= previous, "checkpoints must be time ordered")
            previous = expectation["at_tick"]
            state = expectation["state"]
            fields(state, [], STATE)
            require(bool(state), "empty checkpoint")
            for key, value in state.items():
                bounds = STATE[key]
                if bounds is None:
                    boolean(value)
                else:
                    integer(value, *bounds)
        require(type(run["green"]) is list and len(run["green"]) <= 32,
                "green must be an array of at most 32 authorization windows")
        previous = 0
        for window in run["green"]:
            fields(window, ["after_tick", "before_tick", "payment_required"])
            integer(window["after_tick"], previous, end - 1)
            integer(window["before_tick"], window["after_tick"] + 1, end)
            boolean(window["payment_required"])
            previous = window["before_tick"]
    require(total_actions <= MAX_ACTIONS, "too many actions")
    require(total_ticks <= MAX_TICKS, "scenario exceeds total tick budget")
    return data


def loads(raw):
    require(len(raw) <= MAX_BYTES, "scenario exceeds 65536 bytes")
    # Scan nesting before recursive JSON decoding; ignore braces inside strings.
    depth = 0
    quoted = escaped = False
    for byte in raw:
        if quoted:
            if escaped:
                escaped = False
            elif byte == 92:
                escaped = True
            elif byte == 34:
                quoted = False
        elif byte == 34:
            quoted = True
        elif byte in (91, 123):
            depth += 1
            require(depth <= MAX_DEPTH, "scenario exceeds nesting depth")
        elif byte in (93, 125):
            depth -= 1

    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, f"duplicate JSON key: {key}")
            result[key] = value
        return result

    def constant(value):
        raise ScenarioError(f"non-JSON constant: {value}")

    try:
        return validate(json.loads(raw.decode("utf-8"), object_pairs_hook=pairs,
                                   parse_constant=constant))
    except (UnicodeError, ValueError, TypeError, OverflowError, RecursionError) as error:
        raise ScenarioError(str(error)) from error


def load(path):
    with Path(path).open("rb") as source:
        return loads(source.read(MAX_BYTES + 1))
