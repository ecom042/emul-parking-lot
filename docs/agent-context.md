# Agent context: design rationale and maintenance knowledge

Use this document to understand constraints before changing the laboratory.
The user guides and technical contracts are authoritative for current behavior;
this is not a progress diary, test report or substitute for running checks.

## Retained decisions

- Keep the STM32U5A5 SoC and the ADC learning objective. The lab board
  `parking_lot_u5` (`hardware/boards/ufal/`) is a trimmed copy of
  `nucleo_u5a5zj_q` (same clocks, console and pins) with the lot's devices in
  its DTS, each behind an alias; there is no `zephyr,user` and no overlay. Do
  not replace ADC with a pulse sensor merely to avoid a model incompatibility.
- Use Zephyr's GPIO/ADC/UART/SPI driver paths. Firmware logs must not control
  physics, authorization or display pixels.
- Reuse `zephyr-virtual-board` for generic assembly, scheduling and transports.
  Keep parking behavior in this repository; use additive models, not a runtime
  fork or edits to downloaded dependencies.
- Model two concurrent lanes (entry; exit with payment and display), one point
  vehicle per lane, and a multi-session lot sized by `CONFIG_PARKING_CAPACITY`.
  Each lane is a separate pair of model instances so oracles stay per lane. Pricing, retry counts
  and timing constants are documented teaching defaults, not commercial policy.
- Debugger integration is deferred. Do not enable wildcard GDB or the legacy
  raw-monitor dashboard as a setup workaround.
- Use private runtime pipes plus restricted loopback HTTP for normal student
  operation. Developer Robot control is privileged and can bind wildcard TCP.
- Project-owned source and documentation use Apache-2.0; upstream terms remain.
- Keep the published tree student-facing. Controller solutions, solved probes
  and their validation sources belong in protected local instructor material.
  The public launcher builds only `student/`; packaging uses curated allowlists.
  Never publish instructor/backup refs with `--all` or `--mirror`.

## Why the peripheral extensions exist

The generated `Analog.STM32_ADC` model has a different register layout from U5
ADC4. The real U5 driver waits for LDORDY at ISR bit 12, which the older model
does not implement. Setting only that bit is insufficient: control, resolution
and data registers also differ. The external ADC4 subset preserves real driver
initialization, conversion and interrupt completion. ADC1 remains unsupported.

The generator does not provide the required U5 GPIO EXTI behavior. The inspected
stock WBA alternative did not satisfy separate pending-bank, masking and
repeated-level rules. The custom EXTI subset implements only the GPIO path
needed for the limit switches.

The display must demonstrate SPI learning, so it decodes a documented ST7789V
subset rather than rendering log messages. The `Course` class prefix marks
custom laboratory models, not upstream or manufacturer implementations.

## Student implementation boundary

Document external API ownership and liveness requirements, not a supplied
controller architecture. An asynchronous ADC API can retain caller-owned objects
after a poll deadline; returning is not cancellation. Students must satisfy the
driver's lifetime contract and continue servicing safety deadlines, but choose
and explain their own implementation. Do not restore the solved acquisition,
parser, motor-control or display-rendering examples into this public tree.

Simulator fault checks must actually prevent or delay IRQ completion. A raw
NVIC masking write that does not cause the intended fault is not valid evidence.

## Reset and observation discoveries

- Machine reset can clear CPU entry/vector setup. Reload the same ELF after
  resetting peripherals, through the shared assembly/reset script.
- Renode 1.17 terminal testers are cached by UART object, including across
  machine reset. Calling `Create Terminal Tester` again does not give fresh
  buffers. Clear completed reports and require post-reset source timestamps;
  unfinished lines and externally queued input need separate care.
- Reset cancels only queues owned by the reset model. Deliver old tester input
  before reset or explicitly manage its epoch; do not claim cancellation of an
  unrelated host queue.
- Preserve per-owner run/sequence identities. Equal timestamps across owners
  do not establish a global order. Do not silently widen safety tolerances to
  compensate for observation limits.
- Wrong-port EXTI tests must reach the receiver. Include a matching-port control
  so an unwired input cannot produce a meaningless negative pass.
- Current browser state is a full snapshot, but UART text is a bounded suffix.
  Initial sequence gaps, later gaps and local text eviction must remain visible.

## Tooling compatibility and provenance

The supported official Renode packages are the 1.17.0 portable builds for
macOS arm64 (.NET 8.0.21) and Linux x86_64/arm64 (.NET 8.0.16). All report
`1.17.0+20260906gitf1dd1b4af`, which `lab.py` checks. Its Infrastructure gitlink is
`cd4b002aac2398994c4a61a34a8b31b5700facf2`; do not infer a submodule revision
from an unrelated retained source snapshot. The pinned STM32 HAL revision used
for model analysis is `39130f29ae37c1db34095478ca02b6419b70dcdc`.

Renode renamed `STM32F4_RNG` to `STM32_RNG`. The generator commit in
`requirements-tools.txt` handles that rename. Its package version remains
`0.0.0`: changing Git pins may require `--force-reinstall`, since an ordinary
install can retain the previous same-version source.

A TCP connect/disconnect used as GDB readiness probing consumed the initial stop
on the tested runtime. A log marker avoids that probe, but wildcard GDB binding
is still outside supported student operation.

An old executable wrapper returned success while reporting a missing runtime.
Check actual version output and expected boot/behavior, not exit status alone.
C# source loading and installed package assets must be tested with the selected
runtime; a timeout is never a successful compilation or integration result.

## Known review work

- Raw simultaneous ADC `ADEN|ADDIS` writes have limited semantics in the model.
  The pinned HAL masks command bits, but broader MMIO support needs tests.
- Linux x86_64 has no local runtime evidence (see below). Fixed-clock/RCC accuracy, ADC1/DMA,
  complete display-driver support, durable sessions and hostile-code isolation
  are not implemented guarantees.

## West topology 2 and Zephyr v4.5.0

`west.yml` at this repository's root makes it the manifest repository of a
topology-2 workspace: `west init -m https://github.com/<account>/emul-parking-lot
--mr main` on the pair's fork (or `west init -l` on a local checkout) and
`west update --narrow` put `zephyr/`, `modules/hal/{stm32,cmsis_6}` and
`zephyr-virtual-board/` beside it, and `west packages pip --install` and
`west sdk install -t arm-zephyr-eabi` complete it, so students need only the
Getting Started host dependencies and a fork; submissions are pull requests
from the fork, graded and reviewed in place, never merged. This repository is public,
so instructor-only material (grade computation, plagiarism check, hidden
scenarios, reference solution) is never committed here; the gates and points
are public in `desafio.md`, and `package-lab.py` remains a release/review tool. `lab.workspace()`
finds the closest `.west/` above the lab, reads `zephyr.base`, and refuses a
`VERSION` other than exactly 4.5.0 (a workspace not updated after a manifest
change). `lab.environment` sets `WEST_CONFIG_LOCAL` and `ZEPHYR_BASE` from it,
ignoring inherited values. ZVB is a tooling-only Zephyr module (no CMake);
the lab uses it through `--module` and `PYTHONPATH`, and its west commands
come with the manifest. `source.json` records the Zephyr path and commit.

v4.5.0 uses the same SDK 1.0.1. Its STM32U5 devicetree puts each ADC under a
common node (`adc4_common: adc@46021000` containing `adc4`), so dts2repl now
names the generated model `adc4common` (and adds an unused `adc12common` at
`0x42028000`; ADC1 stays disabled). `hardware/renode/parking.repl` therefore
unregisters `sysbus.adc4common` before registering the course ADC4 model; on
v4.4.0 the name was `adc4`, and a wrong name makes the runtime `init` fail
with only "embedded operation failed". After any Zephyr change, diff the
generated `zvb.repl` against the previous version first.

## Linux host validation

Student documentation covers Linux only. `lab.py` also accepts macOS arm64,
the development host: SDK `zephyr-sdk-1.0.1_macos-aarch64_minimal.tar.xz`
and Renode `renode-1.17.0.osx-arm64-portable.dmg` (SHA-256
`63b1fb691207f503cea937e4ec8fad3e068a517d0abac3c24c0206c62f6c4d12`), with
`Renode.app` copied outside `/Applications` and
`RENODE_ROOT=<dir>/Renode.app/Contents/MacOS`.

A clean Ubuntu 24.04 aarch64 container (2 CPUs, 2 GB RAM) followed
[instalacao.md](instalacao.md) from a student package with the topology-2
workspace and Zephyr v4.5.0: Getting Started apt packages, Renode
linux-arm64 portable, then `west init -l`, `west update --narrow`,
`west packages pip --install` and `west sdk install -t arm-zephyr-eabi`
(workspace 3.7 GB, SDK 1.9 GB), `pip check` clean. `lab.py --headless normal`
gave the expected starter firmware failure, `check-student-launch.py` passed
(build, auth, network audit, teardown), the ztest template suite passed, and
the coverage tool on the instructor solution passed all 19 public and 4 hidden
scenarios (90.73% lines, 7m53s with one Renode at a time). The same results
hold on the macOS development host.

- **Memory.** Renode peaked near 1.7 GB resident on that 2 GB host, against
  about 840 MB on a 16 GB macOS host. Two parallel Renode processes were
  killed by the kernel OOM killer; the harness then reported `runtime
  disconnected`. The tools therefore treat a harness failure as the host
  status `error`, retried alone like a timeout, and size `--jobs` by RAM
  (`workspace.default_jobs`, 2 GB per process).
- **x86_64.** Only emulated with qemu-user in an arm64 VM, which is not a
  usable test bed: the packages, pip and `west update` worked and the SDK
  checksum and `--version` passed, but `arm-zephyr-eabi-gcc` segfaulted during
  the build and Renode's .NET runtime was killed at `--version`. x86_64
  runtime behaviour is therefore not evidenced. The steps are identical apart
  from the asset names; validate on a native x86_64 Linux host (or a VM with
  Rosetta) before relying on it.

## Validation records

Keep command logs, XML, screenshots, source/binary hashes and failure attempts
under ignored build output. Do not commit per-run transcripts, temporary machine
paths, conversation IDs or stage-by-stage handoff reports as product docs.
Never promote copied-ELF tests to fresh-build evidence or selected reruns to an
uninterrupted suite pass. Preserve failed inputs and classify infrastructure
failure separately from firmware behavior.
