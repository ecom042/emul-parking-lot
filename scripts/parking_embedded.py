# Trusted Renode embedded extension, compatible with its IronPython 2 runtime.
# Browser requests never reach this module directly. No listeners are created.
import json
import System

boot = System.Environment.GetEnvironmentVariable("PARKING_BOOT")
reset = System.Environment.GetEnvironmentVariable("PARKING_RESET")
LANES = ["entry", "exit"]
# Fixed owner table: browser text never selects a device path.
PATHS = {
    "entry.physical": "sysbus.gpiob.physicalEntry",
    "entry.protocol": "sysbus.gpioe.protocolsEntry",
    "exit.physical": "sysbus.gpiob.physicalExit",
    "exit.protocol": "sysbus.gpiod.protocolsExit",
}
last_sequences = dict((owner, 0) for owner in PATHS)
devices = {}
last_pixels = None


def include_file(path):
    if any(c in path for c in ['"', ";", "\n", "\r", "\\"]):
        raise Exception("invalid trusted path")
    if not monitor.Parse("include @" + path.replace(" ", "\\ ")):
        raise Exception("script failed")


def lane_device(lane, kind):
    if lane not in LANES:
        raise Exception("unknown lane")
    return devices[lane + "." + kind]


def state():
    global last_pixels
    lanes = dict((lane, {}) for lane in LANES)
    events = {}
    for owner in sorted(PATHS):
        lane, kind = owner.split(".")
        device = devices[owner]
        lanes[lane][kind] = json.loads(device.Snapshot)
        trace = json.loads(device.TraceAfter(last_sequences[owner]))
        events[owner] = trace
        if trace:
            last_sequences[owner] = trace[-1]["seq"]
    result = {"lanes": lanes, "events": events}
    result["display"] = json.loads(devices["display"].Snapshot)
    pixels = devices["display"].PixelState
    if pixels != last_pixels:
        result["pixels"] = pixels
        last_pixels = pixels
    return result


def configure(lane, kind, config):
    protocol = lane_device(lane, "protocol")
    if kind == "camera":
        protocol.ConfigureCamera(
            config["plate"], ",".join(str(c) for c in config["confidences"]),
            config["delay_ticks"], config["fragment_bytes"], config["missing"])
    elif kind == "payment":
        protocol.ConfigurePayment(
            config["delay_ticks"], config["fragment_bytes"],
            config["result"], config["missing"])
    else:
        raise Exception("unknown configuration")


def handle_request(request):
    global last_pixels
    op = request["op"]
    if op == "init":
        include_file(boot)
        for owner in PATHS:
            devices[owner] = monitor.Machine[PATHS[owner]]
        devices["display"] = monitor.Machine["sysbus.spi1.display"]
    elif op == "reset":
        include_file(reset)
        for owner in PATHS:
            last_sequences[owner] = 0
        last_pixels = None
    elif op == "initial":
        lane_device(request["lane"], "physical").ConfigureInitial(
            request["position_mdeg"], request["vehicle_mm"], request["seed"])
    elif op == "configure":
        configure(request["lane"], request["kind"], request["config"])
    elif op == "step":
        ticks = request["ticks"]
        if type(ticks) is not int or not 1 <= ticks <= 100:
            raise Exception("step bound")
        if not monitor.Parse('emulation RunFor "%.2f"' % (ticks / 100.0)):
            raise Exception("finite step failed")
    elif op == "submit":
        if request["owner"] not in PATHS:
            raise Exception("unknown owner")
        command = request["command"]
        return json.loads(devices[request["owner"]].Submit(
            command["v"], command["run"], command["id"], command["at_tick"],
            command["signal"], command["value"]))
    elif op == "replay":
        lane_device("exit", "protocol").ReplayPayment(request["uuid"], request["delay_ticks"])
    elif op != "snapshot":
        raise Exception("unknown operation")
    return state()
