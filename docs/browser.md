# Local browser and runtime interface

This is a local macOS development environment, not a public hosting service,
hostile-firmware sandbox or certified safety system.
Retain `nucleo_u5a5zj_q`, the limited ADC4 channel-18 subset, existing GPIO,
UART/SPI/RCC limitations and the [equipment contracts](device-protocols.md).
Debug remains **DEFERRED**.
The old generic dashboard and its monitor/GDB routes remain disabled.

## Launch and trust boundary

Prepare the student ELF with the established build and
`scripts/prepare-smoke.py` workflow using the pinned ZVB module. The required input is a
trusted prepared `boot.resc` with its sibling `reset.resc`; they use the shared
ZVB machine assembly. No model, nested dependency or runtime installation patch
is required. Then:

```sh
python scripts/parking-web.py \
  --module "$PWD/deps/zephyr-virtual-board" \
  --renode "$RENODE_ROOT/renode" \
  --machine-script "$PWD/build/student/boot.resc" \
  --evidence "$PWD/build/interactive-01"
```

Use a **new evidence directory**. The launcher prints a local capability link;
open it only on this machine. Treat the link as a credential and do not copy it
into retained logs, screenshots or shared documents. Its fragment never reaches
the HTTP server or Referer. The browser removes it immediately and exchanges
it in a protected request for an HttpOnly SameSite=Strict session cookie.
Reloads and new tabs in that browser session reconnect without another token.
Restarting the launcher invalidates all previous capabilities and sessions.
Ctrl-C closes HTTP connections, stops/reaps the owned runtime and retains
source observations. Closing a browser does not stop the engine.

Only HTTP/SSE binds `127.0.0.1:<selected-port>`. The generic transport requires
that exact Host, rejects other supplied Origins and cross-site fetch metadata,
and requires the exact Origin for POST. API data and commands require the
session cookie. No wildcard bind, Robot, UART TCP terminal, raw monitor,
GDB endpoint, unrestricted model-method API, arbitrary path or code field is
part of this workflow. Trusted ELF/model/extension/machine inputs can execute
local code: do not run untrusted course submissions as a security sandbox.

## Runtime and control context

The generic `zvb.stdio.StdioRuntime` reuses the official documented embedded
Python extension path with `--disable-xwt --plain -P -1`, and a process-private
configuration. The trusted `scripts/parking_embedded.py` defines a typed,
bounded private dispatch loop, not an interactive monitor terminal.
Inherited stdin/stdout pipes replace the exposed privileged TCP backend.
It invokes fixed internal assembly/reset/finite-step commands and calls actual
equipment methods. Browser text is never interpolated into monitor syntax.
See the tooling's `docs/stdio-runtime.md` and `docs/local-web.md`.

One engine worker owns this control context. It executes finite steps of at
most five equipment ticks, stopping earlier at scenario action/checkpoint
boundaries, and handles queued requests only while Renode is paused. Browser
commands cannot choose step duration or tick advancement. Optional host pacing
is only presentation speed: the same Renode timers own vehicle/barrier motion,
noise, UART latency and firmware deadlines. There is no browser physics,
firmware-log-driven display or new host-time equipment scheduler.

Every run is finite: a fixture's existing duration, or 6000 ticks / 60 virtual
seconds for manual mode. Reset or select creates a fresh host epoch and fresh
equipment run IDs, cancels pending model work and reloads the same ELF.
Old source-run submissions are deliberately checked to reject after reset.
An epoch scopes browser commands; process-local revisions do not reset with it.

## Student-facing API

`GET /api/snapshot` returns a complete version-1 state:

- Monotonic process-wide `revision`, host `run` epoch, scenario/segment,
  running/completed/failure status.
- Actual physical and protocol snapshots, with independent source `run`,
  `seq`, `tick`, `time_us`, pending and drop counters.
- Display metadata and the actual 240×320 native RGB565 SPI framebuffer.
- At most 128 recent source events per owner, 128 command outcomes and 128
  checkpoint results. Complete bounded run evidence is retained separately.

`GET /api/events` is SSE: `snapshot` starts/restarts every connection, then
`message` publications carry the same complete state shape. Older or duplicate
revisions are ignored by the browser, including queued pre-reset frames.
Per-owner sequence numbers order source events; they do not invent a global
ordering between owners. A source-ring gap fails assessment. A browser recent
window gap is explicitly diagnosed as incomplete UART history while the full
state remains coherent. Reconnect never pretends to replay an evicted prefix.

`POST /api/command` requires JSON, version `v: 1`, current 32-character `run`,
an ASCII alphanumeric `id` (1..64 characters) and exactly the fields for its op:

| `op` | Additional fields | Effect |
|---|---|---|
| `select` | `scenario` from the fixed picker | Coherent reset into a parsed fixture or manual mode |
| `start` | none | Resume the engine until paused, complete or failed |
| `pause` | none | Stop between finite virtual-time steps |
| `reset` | none | Coherent reset of the selected scenario |
| `submit` | `signal`, integer `value` | Scenario allowlist and range/step rules; schedule at next source tick |

The command ID cache admits at most 4096 IDs/epoch, without eviction.
Duplicate IDs reject; do not retry uncertain commands under a new ID.
`reset` and `select` remain available to recover at capacity. The command
queue holds 32 requests; HTTP transport additionally limits command size,
admission/rate/connection resources. Accepted is not executed: the model's
later `executed`/`failed` events remain visible, including rejected next-car
scene preconditions. Failures are shown in the main view and diagnostics.

Equipment actions cannot set motor GPIOs, physical barrier position, ADC code,
arbitrary UART bytes or firmware success. “Car arrival” requests forward
vehicle speed; “Stop car” holds the point. “Next car” retains the established
passed/stopped/barrier-down-and-disabled preconditions. The beam is independent
and must be toggled to represent passage, not inferred from car distance.

Manual mode holds automatic payment replies (`missing: true`). “Pay” schedules
the existing terminal `payment.replay` action, which sends actual LPUART1 bytes
for the retained charge. It never opens the barrier directly; matching UUID,
amount, currency, firmware state and limit feedback remain necessary.
Payment is simulated, not PIX. Fixture mode preserves its original protocol
configuration and expected observations; changing equipment interactively can
legitimately cause a fixture assertion failure.

## View and assessment

The view renders source vehicle position, barrier millidegrees, red/green/
buzzer, independent beam, ADC4 sample, GPIO diagnostics, UART byte events,
SPI diagnostics/framebuffer and test results. Canvas pixels are decoded from
the actual SPI model export, not regenerated labels or logs.

Interactive and browser-free validation import the same strict scenario
parser, continuous motor/obstruction/every-green authorization checks and
checkpoint oracle. Manual mode uses the same continuous safety checks with
bounded broad green windows and alternating entry/paid-exit policy; it does not
claim conformance to a complete fixture. Test results are observations of
device behavior, not firmware log or symbol declarations.

Slow consumers have four pending frames maximum (at most 1 MiB each), plus
bounded OS/socket buffers and a finite write deadline. Publication never waits
on network I/O. Overflow disconnects; EventSource reconnects for a fresh full
snapshot. The process has at most 16 active connections including preauth
requests. A disconnected browser cannot block equipment progress.

## Evidence and limits

`launch.json` records trusted boot hash and runtime argv but never capability
or cookies. Run JSON includes source traces, final state/pixels, commands,
checkpoints and outcome, with distinct reset-scoped identities. Retain the
ignored build directory independently: it does not travel through source merges.
Keep runtime/browser checks distinct from host-only security tests. Preserve
unsuccessful attempts alongside accepted results rather than rewriting them.
Use the [setup guide](setup.md) and published module revision in the manifest.
Sensor subset, fixed-clock/RCC limits, no ADC1/DMA and unvalidated other hosts
remain limitations; see the [release checklist](release-checklist.md).
