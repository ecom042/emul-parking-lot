# emul-parking-lot

A hardware-free embedded-systems laboratory using Zephyr RTOS, Renode and a
restricted local browser view. Students implement a parking controller using
GPIO, ADC, UART and SPI while the environment models vehicles and equipment.

Start with [setup](docs/setup.md), then the
[student specification](docs/student-specification.md).

## Repository boundary

This is the **student-facing source tree**. It contains simulator infrastructure,
an inert starter, device contracts and public behavioral checks—not a completed
parking controller. Instructor solutions, teaching probes and their specific
validation helpers are maintained locally, outside the published history.
The source packager excludes known instructor locations and curates host
infrastructure; source review is still required.

## What runs where

- **Student firmware** controls the motor, signals, recognition, payment and
  sessions through Zephyr drivers.
- **Renode models** own barrier motion, sensor inputs, camera/payment replies
  and pixels decoded from SPI.
- **Scenarios** stimulate equipment and assess actual device behavior.
- **The browser** renders current state and submits validated equipment actions.
  It does not drive physics or turn firmware logs into successful behavior.

The retained target is `nucleo_u5a5zj_q`, with one lane, one point vehicle and
one active session. macOS arm64 is the validated runtime host. These are bounded
teaching models, not general MCU emulation or a certified physical controller.

## Launch

Follow [setup](docs/setup.md) first to install project-local dependencies,
initialize west and select the existing SDK and verified Renode executable.
From the activated environment at the repository root:

```sh
export ZVB_MODULE="$PWD/deps/zephyr-virtual-board"
python scripts/lab.py --module "$ZVB_MODULE" \
  --renode "$RENODE_ROOT/renode" --output build/student-01
```

The launcher builds `student/`, prepares the machine and prints a private local
capability link. The starter is intentionally inert: motor/green/buzzer off,
red on. It does not contain a completed solution and should fail normal
completed-controller scenarios until students implement the requirements.

Use a new output directory for each launch. Closing a tab does not stop the
engine; Ctrl-C stops the launcher and its owned runtime.

Public browser-free practice uses the same models and behavioral assertions:

```sh
python scripts/lab.py --module "$ZVB_MODULE" \
  --renode "$RENODE_ROOT/renode" --output build/student-normal-01 \
  --headless normal
```

## Documentation

| Document | Purpose |
|---|---|
| [Architecture](docs/architecture.md) | Components, ownership, time and trust boundaries |
| [Hardware models](docs/hardware-models.md) | Pin map, ADC4/EXTI/display subsets and limitations |
| [Physical model](docs/physical-model.md) | Motion, sensors, calibration, commands and reset |
| [Device protocols](docs/device-protocols.md) | Camera/payment frames, display and controller requirements |
| [Scenarios](docs/scenarios.md) | Schema, scheduling, assertions, outcomes and developer runner |
| [Browser](docs/browser.md) | Local API, authentication, snapshots, reconnect and containment |
| [Setup](docs/setup.md) | Supported toolchain and build/launch instructions |
| [Student specification](docs/student-specification.md) | Requirements, deliverables and learning objectives |
| [Release checklist](docs/release-checklist.md) | Packaging, pilot and rollout checks |
| [Agent context](docs/agent-context.md) | Design rationale and maintenance discoveries |

## Source layout

`student/` is the application students implement. `hardware/` and `models/`
define shared trusted infrastructure. `scripts/`, `tests/` and `web/` provide
launch, assessment and presentation. Public scenarios and host-side assertions
describe expected behavior; they do not provide firmware state-machine,
parser, ADC-acquisition or display-rendering implementations.

Generic tooling comes from the immutable published `zephyr-virtual-board`
revision in [the west manifest](manifest/west.yml). An attached checkout is
optional for tooling development, not a required student dependency.

## Verification and limitations

Host infrastructure checks:

```sh
python -m unittest discover -s tests -p 'test_*.py' -v
node --test tests/test_browser_history.cjs
```

Host tests are not guest/runtime evidence. Run actual scenarios, negative
controls and lifecycle checks for behavior changes. Keep exact source/binary
identities, logs, screenshots and failed attempts under ignored build output;
do not retain capabilities or session cookies.

Normal launch uses private Renode pipes and loopback-only authenticated HTTP,
not Robot, TCP monitor or GDB. Developer Robot tests have a privileged transport
that may bind wildcard TCP 49152. Neither workflow is a sandbox for hostile ELF
or extension code. Debugging and the legacy raw-monitor dashboard are not
supported student workflows.

ADC1/DMA, full clock/power/electrical accuracy, complete display-driver support,
durable sessions, real payments, collision physics and other host platforms
are outside the supported subset. See the technical contracts before extending it.

## License

Project-owned source and documentation use [Apache-2.0](LICENSE). Existing
third-party notices remain applicable. Downloaded dependencies, Renode, SDK and
browser binaries retain their own licenses and are not bundled by assumption.
