"""JSON Schema export, using the parser's scalar constraints as source of truth.

`python scripts/scenario-schema.py` emits the editor/tooling schema. Runtime
validation additionally enforces duplicate keys, resource budgets and ordering.
"""

from .parser import LANES, MAX_ACTIONS, MAX_RUN_TICKS, PLATE, SIGNALS, STATE


def integer(low, high, step=1):
    return {"type": "integer", "minimum": low, "maximum": high, "multipleOf": step}


def obj(properties, optional=()):
    return {"type": "object", "additionalProperties": False, "properties": properties,
            "required": [key for key in properties if key not in optional]}


def array(items, low=0, high=256):
    return {"type": "array", "minItems": low, "maxItems": high, "items": items}


def ref(name):
    return {"$ref": f"#/$defs/{name}"}


def schema():
    boolean = {"type": "boolean"}
    timing = {"delay_ticks": integer(1, 3000), "fragment_bytes": integer(1, 128),
              "missing": boolean}
    tick = integer(1, MAX_RUN_TICKS)
    lane = {"enum": list(LANES)}
    camera = obj({
        "plate": {"type": "string", "pattern": f"^{PLATE}$"},
        "confidences": array(integer(0, 100), 1, 16), **timing})
    payment = obj({**timing, "result": {"enum": ["PAID", "CANCELLED", "MALFORMED"]}})
    # Payment signals and configuration exist only where the terminal is wired.
    submit = [
        obj({"at_tick": tick, "lane": {"const": "exit"} if signal.startswith("payment.")
             else lane, "kind": {"const": "submit"}, "signal": {"const": signal},
             "value": integer(*bounds)})
        for signal, bounds in SIGNALS.items()
    ]
    configurations = [
        obj({"at_tick": tick, "lane": lane if kind == "camera" else {"const": "exit"},
             "kind": {"const": kind}, "config": ref(kind)})
        for kind in ("camera", "payment")
    ]
    replay = obj({"at_tick": tick, "lane": {"const": "exit"}, "kind": {"const": "replay"},
                  "charge": integer(1, 64), "delay_ticks": integer(1, 3000)})
    state = obj({key: boolean if bounds is None else integer(*bounds)
                 for key, bounds in STATE.items()}, optional=STATE)
    state["minProperties"] = 1
    window = obj({"after_tick": integer(0, MAX_RUN_TICKS - 1), "before_tick": tick})
    initial = obj({"position_mdeg": integer(0, 90000), "vehicle_mm": integer(-2000, 6000)})
    entry = obj({"initial": initial, "camera": ref("camera"),
                 "green": array(window, 0, 32)}, optional=("camera",))
    exit_lane = obj({"initial": initial, "camera": ref("camera"), "payment": ref("payment"),
                     "green": array(window, 0, 32)}, optional=("camera", "payment"))
    run = obj({
        "duration_ticks": tick,
        "lanes": obj({"entry": entry, "exit": exit_lane}),
        "actions": array({"oneOf": submit + configurations + [replay]}, 0, MAX_ACTIONS),
        "expect": array(obj({"at_tick": tick, "lane": lane, "state": ref("state")}), 1, 128),
    })
    root = obj({
        "version": {"type": "integer", "const": 2},
        "name": {"type": "string", "pattern": "^[a-z][a-z0-9-]{0,63}$"},
        "seed": integer(1, 2147483647),
        "capacity": integer(1, 8),
        "deadlines": obj({"motor_ms": {"type": "integer", "const": 2500}}),
        "runs": array(ref("run"), 1, 8),
    }, optional=("capacity",))
    return {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "$id": "urn:emul-parking-lot:scenario:2",
        "title": "Parking scenario v2",
        "description": "Two lanes (entry, exit). The strict parser also accepts "
                       "non-paying v1 fixtures, converts them onto the entry lane, and "
                       "checks byte/depth/total count budgets, time ordering and "
                       "run-relative interval bounds.",
        **root,
        "$defs": {"camera": camera, "payment": payment, "state": state, "run": run},
    }
