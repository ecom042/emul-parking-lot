"""Strict v2 parser (v1 up-converted). No scenario string is executable monitor syntax.

Version 2 describes two independent lanes. The entry lane has a camera, barrier
and signals; the exit lane additionally owns the payment terminal and display.
Every action and checkpoint names its lane. A version-1 single-lane fixture is
accepted only when it never pays: it is converted onto the entry lane, and
the exit lane must then stay closed (no green window).
"""

import copy
import json
from pathlib import Path
import re

MAX_BYTES = 131072
MAX_DEPTH = 12
MAX_ACTIONS = 512
MAX_RUN_TICKS = 30000
MAX_TICKS = 60000
LANES = ("entry", "exit")
PLATE = r"[A-Z]{3}[0-9][A-Z0-9][0-9]{2}"


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
    require(type(value["plate"]) is str and re.fullmatch(PLATE, value["plate"]),
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


def lane_signal(lane, signal):
    """Payment actions exist only where the terminal is wired."""
    require(not signal.startswith("payment.") or lane == "exit",
            "payment signals are only available on the exit lane")


def owner(signal):
    return "protocol" if signal.startswith("payment.") else "physical"


def lane_config(value, end):
    fields(value, ["initial", "green"], ["camera", "payment"])
    fields(value["initial"], ["position_mdeg", "vehicle_mm"])
    integer(value["initial"]["position_mdeg"], 0, 90000)
    integer(value["initial"]["vehicle_mm"], -2000, 6000)
    for key, check in (("camera", camera), ("payment", payment)):
        if key in value:
            check(value[key])
    require(type(value["green"]) is list and len(value["green"]) <= 32,
            "green must be an array of at most 32 authorization windows")
    previous = 0
    for window in value["green"]:
        fields(window, ["after_tick", "before_tick"])
        integer(window["after_tick"], previous, end - 1)
        integer(window["before_tick"], window["after_tick"] + 1, end)
        previous = window["before_tick"]


def action(value, end):
    require(type(value) is dict, "action must be an object")
    kind = value.get("kind")
    lane = value.get("lane")
    require(lane in LANES, "action lane must be entry or exit")
    if kind == "submit":
        fields(value, ["at_tick", "lane", "kind", "signal", "value"])
        require(type(value["signal"]) is str and value["signal"] in SIGNALS,
                "unknown typed signal")
        lane_signal(lane, value["signal"])
        low, high, step = SIGNALS[value["signal"]]
        integer(value["value"], low, high)
        require(value["value"] % step == 0, "invalid signal step")
    elif kind in ("camera", "payment"):
        fields(value, ["at_tick", "lane", "kind", "config"])
        require(kind == "camera" or lane == "exit", "payment configuration is exit-lane only")
        (camera if kind == "camera" else payment)(value["config"])
    elif kind == "replay":
        fields(value, ["at_tick", "lane", "kind", "charge", "delay_ticks"])
        require(lane == "exit", "payment replay is exit-lane only")
        integer(value["charge"], 1, 64)
        integer(value["delay_ticks"], 1, 3000)
    else:
        raise ScenarioError("unknown action kind")
    integer(value["at_tick"], 1, end)


def validate(data):
    if type(data) is dict and type(data.get("version")) is int and data["version"] == 1:
        data = upgrade(data)
    fields(data, ["version", "name", "seed", "deadlines", "runs"], ["capacity"])
    integer(data["version"], 2, 2)
    require(type(data["name"]) is str and
            re.fullmatch(r"[a-z][a-z0-9-]{0,63}", data["name"]), "invalid scenario name")
    integer(data["seed"], 1, 2147483647)
    fields(data["deadlines"], ["motor_ms"])
    integer(data["deadlines"]["motor_ms"], 2500, 2500)
    if "capacity" in data:
        integer(data["capacity"], 1, 8)
    require(type(data["runs"]) is list and 1 <= len(data["runs"]) <= 8, "expected 1..8 runs")
    total_ticks = total_actions = 0
    for run in data["runs"]:
        fields(run, ["duration_ticks", "lanes", "actions", "expect"])
        integer(run["duration_ticks"], 1, MAX_RUN_TICKS)
        end = run["duration_ticks"]
        total_ticks += end
        fields(run["lanes"], LANES)
        for lane in LANES:
            lane_config(run["lanes"][lane], end)
        require("payment" not in run["lanes"]["entry"], "the entry lane has no payment terminal")
        require(type(run["actions"]) is list, "actions must be an array")
        total_actions += len(run["actions"])
        previous = 0
        for value in run["actions"]:
            action(value, end)
            require(value["at_tick"] >= previous, "actions must be time ordered")
            previous = value["at_tick"]
        require(type(run["expect"]) is list and 1 <= len(run["expect"]) <= 128,
                "expected 1..128 checkpoints")
        previous = 0
        for expectation in run["expect"]:
            fields(expectation, ["at_tick", "lane", "state"])
            require(expectation["lane"] in LANES, "checkpoint lane must be entry or exit")
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
    require(total_actions <= MAX_ACTIONS, "too many actions")
    require(total_ticks <= MAX_TICKS, "scenario exceeds total tick budget")
    return data


def upgrade(data):
    """Convert a non-paying v1 single-lane fixture onto the entry lane.

    The v1 lane both admitted and charged vehicles. Payment belongs to the exit
    lane now, so any v1 fixture that pays must be rewritten by hand.
    """
    fields(data, ["version", "name", "seed", "deadlines", "runs"])
    require(type(data["runs"]) is list, "expected runs array")
    result = {k: copy.deepcopy(v) for k, v in data.items() if k != "runs"}
    result["version"] = 2
    result["runs"] = []
    for run in data["runs"]:
        fields(run, ["initial", "duration_ticks", "actions", "expect", "green"],
               ["camera", "payment"])
        require("payment" not in run, "v1 payment fixtures must be rewritten as v2 exit lanes")
        require(type(run["green"]) is list and type(run["actions"]) is list and
                type(run["expect"]) is list, "expected arrays")
        entry = {"initial": copy.deepcopy(run["initial"]), "green": []}
        for window in run["green"]:
            require(type(window) is dict and window.get("payment_required") is False,
                    "v1 paid exits must be rewritten as v2 exit lanes")
            entry["green"].append({k: window[k] for k in ("after_tick", "before_tick")
                                   if k in window})
        if "camera" in run:
            entry["camera"] = copy.deepcopy(run["camera"])
        actions = []
        for item in run["actions"]:
            require(type(item) is dict and item.get("kind") in ("submit", "camera"),
                    "v1 payment actions must be rewritten as v2 exit lanes")
            require("lane" not in item, "v1 actions have no lane field")
            require(item.get("kind") != "submit" or
                    not str(item.get("signal", "")).startswith("payment."),
                    "v1 payment actions must be rewritten as v2 exit lanes")
            actions.append({**copy.deepcopy(item), "lane": "entry"})
        require(all(type(e) is not dict or "lane" not in e for e in run["expect"]),
                "v1 checkpoints have no lane field")
        result["runs"].append({
            "duration_ticks": run["duration_ticks"],
            "lanes": {"entry": entry, "exit": {
                "initial": {"position_mdeg": 0, "vehicle_mm": 6000}, "green": []}},
            "actions": actions,
            "expect": [{**copy.deepcopy(e), "lane": "entry"} if type(e) is dict else e
                       for e in run["expect"]],
        })
    return result


def loads(raw):
    require(len(raw) <= MAX_BYTES, f"scenario exceeds {MAX_BYTES} bytes")
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
