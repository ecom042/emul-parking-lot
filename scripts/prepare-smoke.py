"""Generate the test machine using the pinned tooling's assembly path."""

from pathlib import Path
import argparse
import os
import sys


ROOT = Path(__file__).resolve().parents[1]
MODULE = Path(os.environ.get("ZVB_MODULE_DIR", ROOT / "deps/zephyr-virtual-board"))
sys.path.insert(0, str(MODULE / "src"))

from zvb import _resc, config, repl  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--config", type=Path)
    parser.add_argument("--gpio-hooks", action="store_true",
                        help="attach the shared observation hooks for configured LEDs")
    args = parser.parse_args()
    cfg = config.load(str(args.config.resolve()) if args.config else None,
                      build_dir=str(args.build_dir.resolve()))
    if repl.generate(cfg) is None:
        return 1
    target = args.build_dir / "boot.resc"
    # Do not attach a console or start the CPU: the test attaches its terminal
    # tester before starting emulation, preserving the boot marker.
    commands = _resc.machine_commands(cfg)
    if args.gpio_hooks:
        commands.extend(_resc.gpio_state_hook(led.port, led.pin, led.name)
                        for led in cfg.leds)
    target.write_text("\n".join(commands) + "\n")
    # Reset clears the CPU's ELF entry/vector setup. Restore it from the same
    # configuration, after resetting peripherals, rather than assuming address 0.
    reset = args.build_dir / "reset.resc"
    reset.write_text("machine Reset\nsysbus LoadELF " +
                     _resc.path_argument(cfg.elf) + "\n")
    print(target)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
