#!/usr/bin/env python3
"""Build and launch the supported local parking lab; never enable raw services."""
import argparse
import configparser
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import subprocess
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


# Finite fixtures use the documented 1000 ms tariff unit; the interactive lot
# uses 5000 ms so that stays of a few virtual seconds show different prices.
TARIFF_UNIT_MS = {"headless": 1000, "interactive": 5000}

# West topology 2: this repository is the manifest repository (west.yml) and
# `west update` puts Zephyr, its modules and zephyr-virtual-board beside it.
ZEPHYR_VERSION = (4, 5, 0)

# Hosts with an official Renode 1.17.0 portable build and a Zephyr SDK 1.0.1.
SUPPORTED_HOSTS = {("Darwin", "arm64"), ("Linux", "x86_64"), ("Linux", "aarch64")}

# The official 1.17.0 portable builds all come from commit f1dd1b4af, but each
# package stamps its own build date (macOS 20260906, Linux 20260907).
RENODE_BUILD = re.compile(r"1\.17\.0\+\d{8}gitf1dd1b4af\b")


def renode_identity_ok(version_text):
    return RENODE_BUILD.search(version_text) is not None


# The lab's own board (hardware/boards/ufal/parking_lot_u5), derived from
# ST's nucleo_u5a5zj_q; student/CMakeLists.txt adds hardware/ to BOARD_ROOT.
BOARD = "parking_lot_u5"


def commands(module, renode, output, headless=None, capacity=None, app=None, cmake_args=()):
    """Build, prepare and launch commands. Grading tools pass another `app`
    (a submitted copy of student/) and extra CMake arguments."""
    firmware = output / "firmware"
    unit = TARIFF_UNIT_MS["headless" if headless else "interactive"]
    build = [sys.executable, "-m", "west", "build", "-b", BOARD,
             str(app or ROOT / "student"), "-d", str(firmware), "--",
             f"-DCONFIG_PARKING_TARIFF_UNIT_MS={unit}"]
    if capacity is not None:
        build.append(f"-DCONFIG_PARKING_CAPACITY={capacity}")
    build.extend(cmake_args)
    prepare = [sys.executable, str(ROOT / "scripts/prepare-smoke.py"),
               "--build-dir", str(firmware), "--config",
               str(ROOT / "student/boards/zvb" / BOARD / "zvb.yml")]
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


def workspace_topdir(path):
    """The west workspace containing `path` (the closest .west/), or None."""
    for parent in (path, *path.parents):
        if (parent / ".west/config").is_file():
            return parent
    return None


def workspace():
    """(topdir, ZEPHYR_BASE, None) of the workspace holding this lab, else (None, None, error)."""
    wanted = "v{}.{}.{}".format(*ZEPHYR_VERSION)
    topdir = workspace_topdir(ROOT)
    if topdir is None:
        return None, None, ("initialize the west workspace first: west init -l emul-parking-lot, "
                            "then west update --narrow (see docs/setup.md)")
    config = configparser.ConfigParser()
    config.read(topdir / ".west/config")
    base = (topdir / config.get("zephyr", "base", fallback="zephyr")).resolve()
    try:
        fields = dict(map(str.strip, line.split("=", 1))
                      for line in (base / "VERSION").read_text().splitlines() if "=" in line)
        found = tuple(int(fields[k]) for k in ("VERSION_MAJOR", "VERSION_MINOR", "PATCHLEVEL"))
    except (OSError, KeyError, ValueError):
        return None, None, f"no Zephyr in {base}; run west update --narrow"
    if found != ZEPHYR_VERSION:
        return None, None, (f"the workspace has Zephyr v{'.'.join(map(str, found))} and this lab "
                            f"pins {wanted}; run west update --narrow")
    return topdir, base, None


def environment(module):
    topdir, base, error = workspace()
    if error:
        raise SystemExit(error)
    env = os.environ.copy()
    env.pop("ZVB_CONFIG", None)
    env["WEST_CONFIG_LOCAL"] = str(topdir / ".west/config")
    env["ZEPHYR_BASE"] = str(base)
    env["ZVB_MODULE_DIR"] = str(module)
    env["PYTHONPATH"] = str(module / "src")
    return env


def zephyr_revision(base):
    """The checked-out Zephyr commit, for provenance only."""
    try:
        result = subprocess.run(["git", "-C", str(base), "rev-parse", "HEAD"],
                                capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return result.stdout.strip() if result.returncode == 0 else None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--module", type=Path, required=True,
                    help="reviewed ZVB source checkout or extracted source artifact")
    ap.add_argument("--renode", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True, help="new directory under build/")
    ap.add_argument("--prepare-only", action="store_true")
    ap.add_argument("--capacity", type=int, choices=range(1, 9), metavar="1..8",
                    help="parking lot capacity (default: Kconfig default 4)")
    ap.add_argument("--headless", choices=["all"] + sorted(
        p.stem for p in (ROOT / "tests/scenarios").glob("*.json")))
    args = ap.parse_args()
    if (platform.system(), platform.machine()) not in SUPPORTED_HOSTS:
        ap.error("supported hosts: Linux x86_64/aarch64 and macOS arm64 (see docs/setup.md)")
    if sys.version_info[:2] != (3, 12):
        ap.error("use the documented Python 3.12 virtual environment")
    module, renode, output = (p.resolve() for p in
                              (args.module, args.renode, args.output))
    if not output.is_relative_to(ROOT / "build") or output == ROOT / "build":
        ap.error("--output must be a new directory beneath this worktree's build/")
    for path in ("src/zvb/stdio.py", "src/zvb/local_web.py", "src/zvb/embedded.py",
                 "src/zvb/models/EquipmentTimeline.cs", "LICENSE"):
        if not (module / path).is_file():
            ap.error(f"selected module is missing {path}; use the workspace zephyr-virtual-board (west update)")
    if not renode.is_file() or not os.access(renode, os.X_OK):
        ap.error("--renode must be the verified official native executable")
    sdk = Path(os.environ.get("ZEPHYR_SDK_INSTALL_DIR", ""))
    if not (sdk / "sdk_version").is_file() or (sdk / "sdk_version").read_text().strip() != "1.0.1":
        ap.error("set ZEPHYR_SDK_INSTALL_DIR to the Zephyr SDK 1.0.1 for this host")
    _, base, error = workspace()
    if error:
        ap.error(error)
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
    build, prepare, launch = commands(module, renode, output, args.headless, args.capacity)
    provenance = {
        "v": 1, "host": platform.platform(), "arch": platform.machine(),
        "python": sys.version, "module": str(module), "sdk": str(sdk.resolve()),
        "zephyr": {"base": str(base), "revision": zephyr_revision(base)},
        "app": "student",
        "commands": [build, prepare, launch],
        "parking_sources": source_hashes(ROOT, [
            "student", "hardware", "models", "scripts", "tests", "web",
            "requirements-tools.txt", "west.yml"]),
        "module_sources": source_hashes(module, ["src", "scripts", "pyproject.toml", "LICENSE"]),
    }
    (output / "source.json").write_text(json.dumps(provenance, indent=2) + "\n")
    if bounded.run([str(renode), "--version"], 15, output / "renode-version.log"):
        return 2
    if not renode_identity_ok((output / "renode-version.log").read_text()):
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
