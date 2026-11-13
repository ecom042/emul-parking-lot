#!/usr/bin/env python3
"""Launch the local parking view with a trusted prepared machine."""
import argparse
import hashlib
import json
from pathlib import Path
import signal
import sys
import threading


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--module", type=Path, required=True)
    ap.add_argument("--renode", type=Path, required=True)
    ap.add_argument("--machine-script", type=Path, required=True)
    ap.add_argument("--evidence", type=Path, required=True, help="new output directory")
    ap.add_argument("--port", type=int, default=0)
    args = ap.parse_args()
    root = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(args.module.resolve() / "src"))
    from zvb.stdio import StdioRuntime
    from zvb.local_web import LocalWebServer
    from parking_web import ParkingEngine, built_config
    boot = args.machine_script.resolve()
    reset = boot.with_name("reset.resc")
    if not boot.is_file() or not reset.is_file():
        ap.error("trusted prepared boot.resc and sibling reset.resc required")
    runtime = StdioRuntime(args.renode.resolve(), root / "scripts/parking_embedded.py",
                           environment={"PARKING_BOOT": str(boot), "PARKING_RESET": str(reset)})
    engine = None
    server = None
    done = threading.Event()
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, lambda *_: done.set())
    try:
        engine = ParkingEngine(runtime, root / "tests/scenarios", args.evidence,
                               **built_config(boot))
        assets = {"/": root / "web/index.html", "/app.js": root / "web/app.js",
                  "/style.css": root / "web/style.css"}
        server = LocalWebServer(engine, assets, port=args.port)
        engine.publish = server.publish
        server.start()
        provenance = {"v": 1, "argv": sys.argv, "runtime_argv": runtime.argv,
                      "origin": server.origin, "trusted_boot": str(boot),
                      "boot_sha256": hashlib.sha256(boot.read_bytes()).hexdigest(),
                      "debug": "DEFERRED", "robot": False}
        (args.evidence / "launch.json").write_text(json.dumps(provenance, indent=2) + "\n")
        print("Open this local session (capability stays in browser fragment):", flush=True)
        print(server.bootstrap_url, flush=True)
        done.wait()
    finally:
        if server:
            server.close()
        if engine:
            engine.close()
        else:
            runtime.close()


if __name__ == "__main__":
    main()
