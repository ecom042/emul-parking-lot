"""Cycle-scoped assertions over actual source GPIO/physics/UART observations."""
import re


def frames(trace, device, signal, after, before):
    data = []
    result = []
    for event in trace:
        if event["device"] != device or event["signal"] != signal:
            continue
        data.append(event)
        if event["value"] == 10:
            if after < data[0]["time_us"] and event["time_us"] <= before:
                text = "".join(chr(e["value"]) for e in data)
                if device == "camera":
                    if text.endswith("\r\n"):
                        result.append(text[:-2])
                else:
                    result.append(text[:-1])
            data = []
    return result


def charge_uuids(requests):
    """Extract canonical wire UUIDs without accepting executable firmware text."""
    return list(dict.fromkeys(
        frame.split()[2] for frame in requests if re.fullmatch(
            r"P1 CHARGE [0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-"
            r"[0-9a-f]{4}-[0-9a-f]{12} [A-Z]{3}[0-9][A-Z0-9][0-9]{2} "
            r"(?:[1-9][0-9]{0,5}|1000000) BRL", frame)))


def assert_green_cycle(physical, protocol, payment_required=False):
    """Check the authorization of the last green; return what it established."""
    green = [e for e in protocol if e["signal"] == "green" and e["value"] == 1][-1]
    at = green["time_us"]
    previous = [e["time_us"] for e in protocol
                if e["signal"] == "green" and e["value"] == 0 and e["time_us"] < at]
    after = max(previous, default=-1)
    positions = [e for e in physical if e["signal"] == "position_mdeg"
                 and e["time_us"] <= at]
    assert positions and positions[-1]["value"] == 90000, "Green before current full opening"
    enables = [e for e in physical if e["category"] == "commanded"
               and e["signal"] == "enable" and after < e["time_us"] <= at]
    assert enables and enables[-1]["value"] == 0, "Green with current motor enabled"
    starts = [e for e in enables if e["value"] == 1]
    assert starts, "No motor opening in this authorization cycle"
    opened = starts[-1]["time_us"]
    assert positions[-1]["time_us"] >= opened, "Green relies on historical upper state"
    directions = [e for e in physical if e["category"] == "commanded"
                  and e["signal"] == "direction" and e["time_us"] <= opened]
    assert directions and directions[-1]["value"] == 1, "Current cycle is not opening"
    requests = frames(protocol, "camera", "tx_byte", after, opened)
    captures = [f.split("=")[1] for f in requests
                if re.fullmatch(r"AT\+CAPTURE=[1-9][0-9]{0,8}", f)]
    assert captures, "No camera request in this cycle"
    replies = frames(protocol, "camera", "rx_byte", after, opened)
    recognized = [match.groups() for f in replies if (match := re.fullmatch(
        r'\+PLATE:([1-9][0-9]{0,8}),"([A-Z]{3}[0-9][A-Z0-9][0-9]{2})",'
        r'([0-9]|[1-9][0-9]|100)', f))]
    recognized = [f for f in recognized if f[0] == captures[-1] and int(f[2]) >= 85]
    assert recognized, "No matching accepted camera result before this opening"
    cycle = {"plate": recognized[-1][1], "green_us": at, "opened_us": opened, "charge": None}
    if payment_required:
        charges = frames(protocol, "payment", "tx_byte", after, opened)
        charges = [f.split() for f in charges if f.startswith("P1 CHARGE ")]
        assert charges, "No charge in this exit cycle"
        charge = charges[-1]
        assert len(charge) == 6 and charge[3] == recognized[-1][1] and charge[5] == "BRL"
        paid = f"P1 RESULT {charge[2]} PAID {charge[4]} BRL"
        results = frames(protocol, "payment", "rx_byte", after, opened)
        assert paid in results, "No current matching paid UUID/amount/currency before opening"
        cycle["charge"] = {"uuid": charge[2], "amount": int(charge[4])}
    return cycle
