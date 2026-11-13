# Agent context: design rationale and maintenance knowledge

Use this document to understand constraints before changing the laboratory.
The user guides and technical contracts are authoritative for current behavior;
this is not a progress diary, test report or substitute for running checks.

## Retained decisions

- Keep `nucleo_u5a5zj_q` and the ADC learning objective. Do not switch boards or
  replace ADC with a pulse sensor merely to avoid a model incompatibility.
- Use Zephyr's GPIO/ADC/UART/SPI driver paths. Firmware logs must not control
  physics, authorization or display pixels.
- Reuse `zephyr-virtual-board` for generic assembly, scheduling and transports.
  Keep parking behavior in this repository; use additive models, not a runtime
  fork or edits to downloaded dependencies.
- Model one lane, one point vehicle and one active session. Pricing, retry counts
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

The supported official Renode package is 1.17.0 macOS arm64, reporting
`1.17.0+20260906gitf1dd1b4af`, .NET 8.0.21. Its Infrastructure gitlink is
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
- Only macOS arm64 has runtime evidence. Fixed-clock/RCC accuracy, ADC1/DMA,
  complete display-driver support, durable sessions and hostile-code isolation
  are not implemented guarantees.

## Validation records

Keep command logs, XML, screenshots, source/binary hashes and failure attempts
under ignored build output. Do not commit per-run transcripts, temporary machine
paths, conversation IDs or stage-by-stage handoff reports as product docs.
Never promote copied-ELF tests to fresh-build evidence or selected reruns to an
uninterrupted suite pass. Preserve failed inputs and classify infrastructure
failure separately from firmware behavior.
