# Laboratory architecture

## Purpose and supported scope

This laboratory lets students implement a Zephyr parking controller without
physical equipment. GPIO, ADC, UART and SPI operations drive observable Renode
devices; console messages are diagnostics, not simulation commands.

The current configuration uses `nucleo_u5a5zj_q`, one lane, one point vehicle and
one active parking session. The required workflow is bounded teaching behavior,
not a production parking product or a certified safety controller.
Only macOS arm64 has runtime qualification. See [setup](setup.md) for versions
and [hardware models](hardware-models.md) for peripheral limitations.

## Components and ownership

```text
Student Zephyr firmware
                 |
        Zephyr peripheral drivers
                 |
     Renode MCU peripheral controllers
                 |
      External equipment/physical models
                 |
    Typed, source-timestamped observations
                 |
  Scenario assessment and local browser adapter
```

| Component | Owns | Must not substitute |
|---|---|---|
| Student firmware | Authorization, motor/signals, recognition retries, payments, sessions, tariff | Direct host setters for driver operations |
| MCU models | Register access, bus transfers and interrupt delivery | A high-level `open_barrier` command |
| Physical model | Barrier/vehicle position, limits, obstruction and distance samples | Automatic correction of unsafe firmware |
| Protocol models | Camera recognition results and simulated terminal replies | Direct motor control or real financial operations |
| Display model | Framebuffer decoded from SPI/GPIO transactions | Text extracted from logs |
| Scenario runner | Typed stimuli, deadlines and behavioral assertions | Required firmware function names or success messages |
| Browser | Rendering, diagnostics and validated user requests | Physics, authoritative time or authorization |

Motor enable and direction GPIOs cause motion. Physical endpoints produce
limit inputs through GPIO/EXTI. Firmware must disable the motor. Reaching an
endpoint, receiving payment or printing a success message does not itself
complete the required control action.

## Repository boundaries

This is the student-facing repository. Instructor solutions and parking-specific
probes are local-only material, not dependencies of student setup or packaging.
The normal launcher builds only `student/`; no reference-firmware option exists.

| Path | Role |
|---|---|
| `student/` | Inert starter students implement |
| `hardware/` | Shared public devicetree, bindings and Renode wiring |
| `models/` | Course-owned peripheral, physical and protocol models |
| `scripts/scenario/`, `tests/scenarios/` | Bounded scenario parsing and assessment |
| `scripts/parking_web.py`, `web/` | Domain adapter and local browser view |
| `scripts/lab.py` | Supported build/prepare/launch entry point |
| `docs/agent-context.md` | Design rationale and maintenance discoveries |

The separate `zephyr-virtual-board` dependency owns generic machine assembly,
configuration discovery, virtual-time scheduling, private stdio control and
restricted HTTP/SSE transport. Parking rules stay in this repository. Use its
published commit selected by `manifest/west.yml`; do not patch downloaded
dependencies to hide compatibility problems.

## Time and lifecycle

Renode virtual time drives firmware deadlines, barrier motion, noise, protocol
assembly timeouts and reply delivery. Equipment integrates every 10 ms. Host
pacing changes presentation speed, not physical or billing time.

Accepted commands are not completed commands. They execute FIFO for equal ticks
within an owner. A successful callback emits `executed`; a throwing callback
emits `failed`, consumes its ID and does not automatically retry partial side
effects. Other queued work continues. Integration failures are not converted
into successful command execution.

Pause freezes virtual progress. Coherent reset clears firmware sessions,
peripheral state, owned pending work and traces, then reloads the ELF entry/
vector state. Run IDs change; old commands reject. Absolute machine timestamps
may continue while run-relative ticks restart at zero.

Physical and protocol owners have independent sequence counters. They do not
establish an exact global order for different owners' equal-time events.
Assessment must retain that observability limit; see [scenarios](scenarios.md).

## Runtime and browser boundary

The normal launcher uses trusted embedded Python and inherited pipes with Renode
`--disable-xwt --plain -P -1`. No Robot service, TCP monitor, UART socket or GDB
endpoint is required. HTTP/SSE binds literal `127.0.0.1`; Host/Origin validation,
capability bootstrap, session authentication and resource bounds protect the
browser API. See [browser](browser.md).

This is not a sandbox for hostile firmware or extensions. Trusted ELF, model,
script and prepared-machine inputs can execute local code. Official developer
Robot tests use a separate privileged transport that may bind wildcard TCP 49152.
Do not treat their headless execution as network containment.

## Verification and delivery

Use host tests for parsing, process lifecycle, transport and packaging; use
actual student Zephyr/Renode integration for peripheral and workflow claims.
Instructor validation may use local completed and deliberately faulty firmware;
those implementations are not distributed here. Negative controls must fail
the intended behavioral rule, not merely crash.
Screenshots, mocked checks and historical counts do not replace current device
evidence.

Keep logs, XML, screenshots, runtime/source hashes and interrupted attempts in
ignored build output. Record the exact inputs and environment; distinguish fresh
builds from reused ELFs and full runs from selected reruns.

Project-owned source and documentation use [Apache-2.0](../LICENSE).
Dependencies retain their own notices. Direct dependency pins do not lock every
transitive Python/PEP 517 input or the remotely fetched SVD. Classroom rollout,
human usability, other hosts and redistribution require the
[release checklist](release-checklist.md).
