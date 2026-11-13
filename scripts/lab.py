#!/usr/bin/env python3
"""Build and launch the supported local parking lab; never enable raw services."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import sys

ROOT = Path(__file__).resolve().parents[1]


def source_hashes(root, directories):
    """Identify actual source bytes, including unpublished local additions."""
    files = []
    for name in directories:
        path = root / name
        files.extend(path.rglob("*") if path.is_dir() else [path])
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(files) if p.is_file() and
            "__pycache__" not in p.parts and p.suffix != ".pyc"}


def commands(module, renode, output, headless=None):
    firmware = output / "firmware"
    build = [sys.executable, "-m", "west", "build", "-b", "nucleo_u5a5zj_q",
             str(ROOT / "student"), "-d", str(firmware)]
    prepare = [sys.executable, str(ROOT / "scripts/prepare-smoke.py"),
               "--build-dir", str(firmware), "--config",
               str(ROOT / "student/boards/zvb/nucleo_u5a5zj_q/zvb.yml")]
    common = ["--module", str(module), "--renode", str(renode),
              "--machine-script", str(firmware / "boot.resc")]
    if headless:
        launch = [sys.executable, str(ROOT / "scripts/assess.py"),
                  *common, "--output", str(output / "assessment")]
        if headless != "all":
            launch += ["--scenario", headless]
    else:
        launch = [sys.executable, str(ROOT / "scripts/parking-web.py"),
                  *common, "--evidence", str(output / "runtime")]
    return build, prepare, launch


def environment(module):
    env = os.environ.copy()
    env.pop("ZVB_CONFIG", None)
    env["WEST_CONFIG_LOCAL"] = str(ROOT / ".west/config")
    env["ZEPHYR_BASE"] = str(ROOT / "deps/zephyr")
    env["ZVB_MODULE_DIR"] = str(module)
    env["PYTHONPATH"] = str(module / "src")
    return env


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--module", type=Path, required=True,
                    help="reviewed ZVB source checkout or extracted source artifact")
    ap.add_argument("--renode", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True, help="new directory under build/")
    ap.add_argument("--prepare-only", action="store_true")
    ap.add_argument("--headless", choices=["all"] + sorted(
        p.stem for p in (ROOT / "tests/scenarios").glob("*.json")))
    args = ap.parse_args()
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        ap.error("only macOS arm64 is an evidenced host; Linux/Windows are unvalidated")
    if sys.version_info[:2] != (3, 12):
        ap.error("use the documented Python 3.12 virtual environment")
    module, renode, output = (p.resolve() for p in
                              (args.module, args.renode, args.output))
    if not output.is_relative_to(ROOT / "build") or output == ROOT / "build":
        ap.error("--output must be a new directory beneath this worktree's build/")
    for path in ("src/zvb/stdio.py", "src/zvb/local_web.py", "src/zvb/embedded.py",
                 "src/zvb/models/EquipmentTimeline.cs", "LICENSE"):
        if not (module / path).is_file():
            ap.error(f"selected module is missing {path}; use the manifest revision")
    if not renode.is_file() or not os.access(renode, os.X_OK):
        ap.error("--renode must be the verified official native executable")
    sdk = Path(os.environ.get("ZEPHYR_SDK_INSTALL_DIR", ""))
    if not (sdk / "sdk_version").is_file() or (sdk / "sdk_version").read_text().strip() != "1.0.1":
        ap.error("set ZEPHYR_SDK_INSTALL_DIR to the existing arm64 Zephyr SDK 1.0.1")
    if not (ROOT / ".west/config").is_file():
        ap.error("initialize the documented local west workspace first")
    try:
        output.mkdir(parents=True, exist_ok=False)
    except FileExistsError:
        ap.error("output exists; choose a new evidence directory (nothing was removed)")
    spec = importlib.util.spec_from_file_location("bounded", ROOT / "scripts/bounded-command.py")
    bounded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bounded)
    os.chdir(ROOT)
    selected_environment = environment(module)
    os.environ.clear()
    os.environ.update(selected_environment)
    build, prepare, launch = commands(module, renode, output, args.headless)
    provenance = {
        "v": 1, "host": platform.platform(), "arch": platform.machine(),
        "python": sys.version, "module": str(module), "sdk": str(sdk.resolve()),
        "app": "student",
        "commands": [build, prepare, launch],
        "parking_sources": source_hashes(ROOT, [
            "student", "hardware", "models", "scripts", "tests", "web",
            "manifest", "requirements-tools.txt"]),
        "module_sources": source_hashes(module, ["src", "scripts", "pyproject.toml", "LICENSE"]),
    }
    (output / "source.json").write_text(json.dumps(provenance, indent=2) + "\n")
    if bounded.run([str(renode), "--version"], 15, output / "renode-version.log"):
        return 2
    if "1.17.0+20260906gitf1dd1b4af" not in (output / "renode-version.log").read_text():
        print("Wrong runtime identity; inspect renode-version.log.", file=sys.stderr)
        return 2
    for command, timeout, name in ((build, 300, "build"), (prepare, 60, "prepare")):
        code = bounded.run(command, timeout, output / f"{name}.log")
        if code:
            print(f"{name} failed ({code}); inspect {output / (name + '.log')}", file=sys.stderr)
            return code
    provenance["artifacts"] = source_hashes(output, [
        "firmware/zephyr/zephyr.elf", "firmware/zephyr/zephyr.dts",
        "firmware/zephyr/.config", "firmware/zvb.repl",
        "firmware/boot.resc", "firmware/reset.resc"])
    (output / "source.json").write_text(json.dumps(provenance, indent=2) + "\n")
    if args.prepare_only:
        print(f"Prepared student firmware; no simulator launched. Evidence: {output}")
        return 0
    if args.headless:
        return bounded.run(launch, 600, output / "headless.log")
    # Existing launcher owns signal handling/teardown; never log its capability.
    os.execve(sys.executable, launch, os.environ.copy())


if __name__ == "__main__":
    raise SystemExit(main())
