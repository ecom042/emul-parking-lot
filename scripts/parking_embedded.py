# Trusted Renode embedded extension, compatible with its IronPython 2 runtime.
# Browser requests never reach this module directly. No listeners are created.
import json
import System

boot = System.Environment.GetEnvironmentVariable("PARKING_BOOT")
reset = System.Environment.GetEnvironmentVariable("PARKING_RESET")
last_sequences = {"physical": 0, "protocol": 0}
devices = {}
last_pixels = None


def include_file(path):
    if any(c in path for c in ['"', ";", "\n", "\r", "\\"]):
        raise Exception("invalid trusted path")
    if not monitor.Parse("include @" + path.replace(" ", "\\ ")):
        raise Exception("script failed")


def state():
    global last_pixels
    result = {}
    events = {}
    for name in ["physical", "protocol"]:
        device = devices[name]
        result[name] = json.loads(device.Snapshot)
        trace = json.loads(device.TraceAfter(last_sequences[name]))
        events[name] = trace
        if trace:
            last_sequences[name] = trace[-1]["seq"]
    result["display"] = json.loads(devices["display"].Snapshot)
    pixels = devices["display"].PixelState
    if pixels != last_pixels:
        result["pixels"] = pixels
        last_pixels = pixels
    result["events"] = events
    return result


def configure(kind, config):
    if kind == "camera":
        devices["protocol"].ConfigureCamera(
            config["plate"], ",".join(str(c) for c in config["confidences"]),
            config["delay_ticks"], config["fragment_bytes"], config["missing"])
    elif kind == "payment":
        devices["protocol"].ConfigurePayment(
            config["delay_ticks"], config["fragment_bytes"],
            config["result"], config["missing"])
    else:
        raise Exception("unknown configuration")


def handle_request(request):
    global last_pixels
    op = request["op"]
    if op == "init":
        include_file(boot)
        devices["physical"] = monitor.Machine["sysbus.gpiob.physical"]
        devices["protocol"] = monitor.Machine["sysbus.gpiod.protocols"]
        devices["display"] = monitor.Machine["sysbus.spi1.display"]
    elif op == "reset":
        include_file(reset)
        last_sequences.update(physical=0, protocol=0)
        last_pixels = None
    elif op == "initial":
        devices["physical"].ConfigureInitial(
            request["position_mdeg"], request["vehicle_mm"], request["seed"])
    elif op == "configure":
        configure(request["kind"], request["config"])
    elif op == "step":
        ticks = request["ticks"]
        if type(ticks) is not int or not 1 <= ticks <= 100:
            raise Exception("step bound")
        if not monitor.Parse('emulation RunFor "%.2f"' % (ticks / 100.0)):
            raise Exception("finite step failed")
    elif op == "submit":
        command = request["command"]
        return json.loads(devices[request["owner"]].Submit(
            command["v"], command["run"], command["id"], command["at_tick"],
            command["signal"], command["value"]))
    elif op == "replay":
        devices["protocol"].ReplayPayment(request["uuid"], request["delay_ticks"])
    elif op != "snapshot":
        raise Exception("unknown operation")
    return state()
