# Trusted Renode embedded extension, compatible with its IronPython 2 runtime.
# Browser requests never reach this module directly. No listeners are created.
import json
import System

boot = System.Environment.GetEnvironmentVariable("PARKING_BOOT")
reset = System.Environment.GetEnvironmentVariable("PARKING_RESET")
# Coverage runs only: a trusted directory for the console and one gcov dump
# per machine run (scripts/grading/gcov.py reads them).
trace_dir = System.Environment.GetEnvironmentVariable("PARKING_TRACE_DIR")
dumps = [0]
# Whether the machine ran since boot or the last reset (nothing to dump otherwise).
ran = [False]
# scripts/grading/gcov_module/dump_irq.c: pending this IRQ dumps the counters.
GCOV_IRQ = 112
DUMP_START = "GCOV_COVERAGE_DUMP_START"
DUMP_END = "GCOV_COVERAGE_DUMP_END"
DUMP_STEP_S = 0.005
DUMP_STEPS = 400
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


console = []


def start_trace():
    """Coverage runs: collect the console, where the gcov dumps arrive."""
    if not trace_dir or console:
        return
    if any(c in trace_dir for c in ['"', ";", "\n", "\r", "\\"]):
        raise Exception("invalid trace directory")
    console.append("")

    def received(value):
        console.append(chr(value))
    monitor.Machine["sysbus.usart1"].CharReceived += received


def read_console():
    text = "".join(console)
    del console[:]
    console.append(text)
    return text


def dump_coverage():
    """Coverage runs: make the firmware dump its gcov counters (one machine
    run's worth: they restart at every reset) and keep that dump in its own
    file. A firmware that cannot answer leaves no file; the host reports it."""
    if not trace_dir:
        return
    if not ran[0]:
        return
    try:
        _dump()
    except Exception as error:
        System.IO.File.AppendAllText(trace_dir + "/dump-errors.txt", str(error) + "\n")


def _dump():
    before = read_console().count(DUMP_END)
    # The NVIC is CPU-local (not on sysbus for the host): pulse its IRQ line.
    for level in ("true", "false"):
        if not monitor.Parse("nvic0 OnGPIO %d %s" % (GCOV_IRQ, level)):
            raise Exception("cannot pend the gcov dump interrupt")
    for _ in range(DUMP_STEPS):
        text = read_console()
        if text.count(DUMP_END) > before:
            end = text.rindex(DUMP_END)
            start = text.rindex(DUMP_START, 0, end)
            dumps[0] += 1
            System.IO.File.WriteAllText(trace_dir + "/console.log", text)
            System.IO.File.WriteAllText("%s/dump-%04d.txt" % (trace_dir, dumps[0]),
                                        text[start:end + len(DUMP_END)] + "\n")
            return
        if not monitor.Parse('emulation RunFor "%.3f"' % DUMP_STEP_S):
            break
    System.IO.File.AppendAllText(trace_dir + "/dump-errors.txt",
                                 "no gcov dump after %.1f s of virtual time\n"
                                 % (DUMP_STEP_S * DUMP_STEPS))


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
        start_trace()
        for owner in PATHS:
            devices[owner] = monitor.Machine[PATHS[owner]]
        devices["display"] = monitor.Machine["sysbus.spi1.display"]
    elif op == "reset":
        dump_coverage()
        include_file(reset)
        ran[0] = False
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
        ran[0] = True
    elif op == "submit":
        if request["owner"] not in PATHS:
            raise Exception("unknown owner")
        command = request["command"]
        return json.loads(devices[request["owner"]].Submit(
            command["v"], command["run"], command["id"], command["at_tick"],
            command["signal"], command["value"]))
    elif op == "replay":
        lane_device("exit", "protocol").ReplayPayment(request["uuid"], request["delay_ticks"])
    elif op == "coverage_dump":
        dump_coverage()
    elif op != "snapshot":
        raise Exception("unknown operation")
    return state()
