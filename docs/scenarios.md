# Scenarios and behavioral assessment

## Scope and entry points

The Python scenario package in `scripts/scenario/` owns parking-specific
validation, trusted Robot adaptation and assessment. The normal student
launcher runs public fixtures through private runtime pipes; see [setup](setup.md).
The developer Robot entry point below reuses prepared ZVB machine assembly,
the same equipment models and `scripts/bounded-command.py`. It does not start
a browser, debugger, interactive monitor or second physics clock.

This is functional assessment for the course model, not physical safety
certification, payment authentication, or a security sandbox for hostile ELFs.
The official privileged Robot transport can listen on wildcard TCP **49152**.
Use a trusted isolated host with inbound filtering. Do not run concurrent
official test processes sharing that port. The normal student workflow remains
separate; headless does not mean listener-free.

Use the [supported toolchain and pinned dependencies](setup.md).
Prepare the student build with `scripts/prepare-smoke.py`.
The machine must include physical `ConfigureInitial(positionMillidegrees,
vehicleMillimetres, seed)` and typed `fault.limits`.

```sh
# Standard library only for parsing/export/oracle host tests:
python -m unittest discover -s tests -p 'test_scenario.py' -v
python scripts/scenario-schema.py > build/scenario-v1.schema.json

# Use the same Python environment as the established official Robot workflow.
export PYTHONPATH="$PWD/scripts:$PWD/tests"
python scripts/scenario-run.py \
  --renode-root "$RENODE_ROOT" \
  --machine-script "$PWD/build/student/boot.resc" \
  --output build/assessment-01 --timeout 120 \
  tests/scenarios/normal.json tests/scenarios/jam.json
```

`--output` must name a new directory; existing evidence is never reused as a
fresh pass. One invocation accepts at most 64 scenarios and runs them
sequentially, each in its own official Robot process. A frozen, validated
scenario copy is passed to Robot, so concurrent edits cannot silently change
the executed stimuli. The machine script is a trusted instructor input, not a
scenario command. Build/prepare is explicit, not performed implicitly by the
assessment runner.

For individual official-runner diagnosis, the reusable suite is
`tests/scenario.robot`. It takes `MACHINE_SCRIPT`, `SCENARIO` and
`SCENARIO_RESULT` variables. Direct use requires the same `PYTHONPATH` and
an outer `bounded-command.py` watchdog; prefer `scenario-run.py`.

## Versioned schema and strict parsing

`scripts/scenario/schema.py` exports JSON Schema Draft 2020-12 with identity
`urn:emul-parking-lot:scenario:1`. Generate machine-readable JSON with
`scenario-schema.py`. Scalar ranges and signal names come from the parser's
shared tables, not a manually maintained second schema file.

The schema is an editor/tooling aid. **The strict parser is authoritative**:
JSON Schema alone cannot reject duplicate keys or enforce every resource/time
relationship. The parser validates all supplied files before launching Renode.
It accepts UTF-8 JSON only, rejects duplicate keys at every nesting level,
unknown fields everywhere, nonstandard `NaN`/`Infinity`, wrong types, floats
where integers are required (including `1.0`), booleans used as integers, and
raw monitor/method actions.

| Resource | v1 bound |
|---|---|
| Encoded input | 65536 bytes, bounded read before decode |
| Object/array nesting | 12, scanned before recursive JSON decoding |
| Runs/scenario | 1..8 |
| Duration/run | 1..6000 ticks |
| Sum of run durations | <=12000 ticks |
| Typed actions/scenario | <=256 |
| Checkpoints/run | 1..128 |
| Green authorization windows/run | 0..32 |
| Camera confidence sequence | 1..16 values, each 0..100 |
| Camera/payment latency | 1..3000 ticks |
| Fragment size | 1..128 bytes |
| Retained host events | <=250000 per owner/run; overflow is a harness failure |

A tick is **10 ms of Renode virtual time**, not wall time. Fields:

```json
{
  "version": 1,
  "name": "stationary",
  "seed": 1,
  "deadlines": {"motor_ms": 2500},
  "runs": [{
    "initial": {"position_mdeg": 0, "vehicle_mm": 800},
    "duration_ticks": 900,
    "actions": [],
    "green": [
      {"after_tick": 0, "before_tick": 600, "payment_required": false}
    ],
    "expect": [
      {"at_tick": 900, "state": {
        "position_mdeg": 90000, "enable": false,
        "green": true, "captures": 1, "vehicle_mm": 800
      }}
    ]
  }]
}
```

`name` is `[a-z][a-z0-9-]{0,63}`. Seed is a positive signed 32-bit integer.
Position is 0..90000 millidegrees; vehicle position is -2000..6000 mm.
The nominal motor budget is fixed at 2500 ms, not weakenable by the input.
Initial position is a **paused Tick 0 setup** only, never an action that moves
the barrier during assessment. Motor enable/direction remain firmware-owned.

Optional per-run `camera`:

```json
{"plate":"ABC1D23","confidences":[62,79,93],
 "delay_ticks":10,"fragment_bytes":2,"missing":false}
```

Optional `payment`:

```json
{"delay_ticks":150,"fragment_bytes":2,"result":"PAID","missing":false}
```

Their omission selects the documented model defaults, restored on every reset.
Payment results are exactly `PAID`, `CANCELLED`, `MALFORMED`. Plates use the
documented [seven-character syntax](device-protocols.md#camera-at-interface).
These settings configure equipment replies;
they do not instruct firmware to authorize anything.

## Typed actions, scheduling and checkpoints

Actions and checkpoints must be time ordered. Times are absolute **run-relative**
ticks, 1..`duration_ticks`. Equal action ticks preserve array FIFO order for
submissions to one owner; there is no new global atomic cross-owner ordering.

```json
{"at_tick":600,"kind":"submit","signal":"vehicle.speed","value":-2000}
```

Allowed signals and integer values:

| Signal | Value |
|---|---|
| `vehicle.speed` | -2000..2000 mm/s, multiples of 100 |
| `vehicle.next` | 1, model rechecks passed/stationary/closed/disabled preconditions |
| `sensor.obstruction` | 0 or 1 |
| `sensor.noise` | 0..64 ADC codes |
| `sensor.seed` | 1..2147483647 |
| `fault.jam` | 0 or 1 |
| `fault.limits` | 0 normal, 1 missing upper, 2 missing lower, 3 both high |
| `payment.replay`, `payment.cancel` | 1, existing typed terminal actions |

The adapter pauses at `at_tick - 1`, binds the current owner's run ID, submits
with a unique command ID and the requested future tick, and verifies acceptance.
Model execution failures such as invalid `vehicle.next` are harness failures,
not successful assessment of firmware. Commands cannot set motor GPIOs, ADC
codes, arbitrary limits, or barrier position.

`kind: "camera"` / `"payment"` actions have `config` objects with the same
shape as initial configurations. They execute paused at `at_tick - 1`,
immediately before that tick's integration; they do not rewrite retained
requests. `kind: "replay"` takes `charge` (1-based ordinal of distinct actual
observed charge UUIDs) and `delay_ticks`. It calls the bounded retained-charge
helper, not arbitrary UART injection. Only strictly parsed canonical charge
frames can supply a UUID to that helper. Replaying an unsettled/nonexistent
charge is a harness failure.

Checkpoints sample actual paused model snapshots after `at_tick`. Supported
state keys:

- Physical: `position_mdeg`, `vehicle_mm`, `enable`.
- Position inequalities: `position_min_mdeg`, `position_max_mdeg`.
- Protocol: `captures`, `payments`, `replies`, `pending`, `red`, `green`, `buzzer`.
  `pending` is the protocol owner's combined scheduled host/reply work count.
- Derived source-transition totals in the current run: `green_count`,
  `buzzer_count`.

Values are exact equality unless explicitly named minimum/maximum. A checkpoint
is not a wait-until loop; a missed deadline fails at its specified virtual tick.
No firmware stdout, symbol names, source-code pattern or declared success flag
determines a behavioral pass.

## Continuous safety and every-green authorization

Every finite step harvests source physical and protocol traces. Steps end at
all scheduled boundaries and at least once per 100 ticks (one virtual second).
Per-owner sequence/run IDs deduplicate the 8192-event rings. Gaps, unexpected
run changes, backwards timestamps, old-run events or capacity exhaustion fail
as harness errors rather than permitting an incomplete-trace pass. Cumulative
model `dropped` alone is not an error when all earlier events were already
harvested without gaps.

The motor check uses actual commanded-enable edges, retaining **every**
completed interval and the trailing still-enabled interval up to the current
source snapshot timestamp. It checks before scenario expectations, so a jammed
deadline mutant fails as `motor_deadline`, not merely a generic final-state
mismatch. Direction reversal, endpoint clamping, jam, host stepping and trace
harvest do not restart the interval. The nominal 2500 ms firmware budget has a
single **10 ms servicing/tick tolerance** (failure above 2510 ms), not a
per-step allowance. This accommodates the finite controller sampling loop;
the model's `enabled_deadline` diagnostic is evidence, not the oracle.

The same-owner physical sequence continuously checks obstruction safety:
closing cannot start with an already-active beam; an obstruction during closing
must be followed by motor disable within 10 ms; a partial-position fault cannot
be followed by automatic direction reversal or another enabled drive before
reset. Clearing the beam does not repair that partial-position fault. The
initial obstruction fixtures therefore assess unsafe intervals as well as
their final snapshots.

Each `green` window expects exactly one green-on transition, in order, with
`after_tick < observed_tick <= before_tick`. An empty array prohibits every
green-on transition. Every observed green, including early/bad transitions
followed by a later good one, is checked using the cycle-scoped
`scenario.observations.assert_green_cycle` over the prefix ending at that green:

- Current physical position is 90000, current commanded motor is disabled.
- This cycle includes an actual opening direction/enable, not historical full
  opening.
- The current camera capture ID has a matching valid plate/confidence >=85
  before opening.
- A `payment_required: true` exit additionally needs that cycle's recognized
  plate, immutable current charge UUID/amount/currency and exact matching paid
  response before opening.

Entry/exit policy is explicit in the scenario windows rather than inferred from
firmware logs or a model payment counter. Unexpected/absent transitions fail.
The model/source serialized callback contract still governs equal timestamps;
per-owner sequence numbers do not invent a global cross-owner order.
Specifically, a physical change serialized after green at exactly the same
timestamp can appear in the physical prefix. These observations cannot prove
every transient same-timestamp cross-owner GPIO ordering. This limitation is
separate from the 10 ms motor servicing tolerance. The required premature-green
variant starts at position zero and is not hidden by this ambiguity. Exact
cross-owner ordering would need a shared ordinal or contemporaneous state
capture; current assessment does not claim it.

## Reset and time semantics

The first run resets the emulation and includes the supplied prepared
`boot.resc` once. Each subsequent run includes its sibling `reset.resc` on
the **same machine**, resetting CPU/peripherals and reloading the same ELF as
in the [reset contract](physical-model.md#time-ordering-reset).
This tests cancellation of pending owner work, not destruction
and replacement of the machine.

Before new configuration, each owner must report Tick 0 and no pending work.
Its run ID must change; a command deliberately using the previous run ID must
reject with `stale_run`. Configuration defaults, seed, requests, pending replies
and traces are reset. Each run retains separate source traces and IDs. Source
`time_us` is **absolute machine elapsed virtual time**, which need not reset;
`tick` restarts at zero. No subtraction rewrites source timestamps.

`reset-capture.json` and `reset-payment.json` require an actual pending reply
before reset, then wait beyond both its prior absolute due tick and the reset
run's relative delay, requiring zero new captures/charges/replies with no
opening. They do not use UART testers or enqueue host-owned bytes outside the
equipment reset boundary.

## Results, watchdog and cleanup

The CLI writes `results.json` and prints its JSON summary to stdout. Exit:

| Status | Exit | Meaning |
|---|---|---|
| `pass` | 0 | All behavioral oracles and checkpoints passed; official runner clean |
| `firmware_failure` | 1 | Complete source evidence violates a behavioral requirement |
| `harness_failure` | 2 | Parse/setup/transport/run/trace/command/watchdog/runner failure |

Each scenario evidence directory contains:

- Frozen `scenario.json`, with its SHA-256 recorded in the summary.
- `assessment.json`: status/reason, seed, run IDs, full deduplicated source
  physical/protocol events, source-timestamped checkpoints and last snapshots.
- `runner.log`, `runner.log.json`: exact official-runner argv, cwd, wall watchdog,
  elapsed monotonic time, timeout/cancellation and process return code.
- Official Robot XML/log/report and retained Renode output under `robot/`.

The summary records prepared machine-script path/hash. Build ELF/config/model/
toolchain provenance should be retained by the invoking validation workflow;
a script hash alone is not a claim of binary provenance.
Failed setup can produce no per-scenario assessment; the top-level status then
remains `harness_failure`, never a stale pass. A new output directory gets a
durable `results.json` even on parse/preflight/setup failure; the aggregate is
updated after every scenario as well as printed on stdout. Existing directories
are rejected without modifying their evidence. Official `robot_output.xml` is
required and independently checked for setup/teardown errors and agreement
with the assessment. An outer runner or cleanup failure cannot become a pass
or a firmware-only failure even when an inner assertion already wrote a result.

`--timeout` is finite 1..3600 seconds **per scenario**, default 120. Official
Robot also gets a finite test timeout. `bounded-command.py` owns the spawned
process group, killing/reaping it on timeout or cancellation and retaining
metadata. Normal official teardown closes the machine/runtime. Descendants
that deliberately create new sessions remain outside the existing watchdog
contract; no unrelated process is searched for or killed. This is the existing
POSIX host workflow, not a new cross-platform lifecycle claim. Cancellation
stops the scenario loop immediately: remaining scenarios never acquire the
control port, and the partial aggregate is retained as a harness failure.

## Initial fixture coverage and validation boundary

| Requirement | Fixture |
|---|---|
| Entry, parking, payment, exit | `normal.json` |
| Confidence 62 → 79 → 93 | `confidence-retry.json` |
| Repeated low confidence; camera timeout | `low-confidence.json`, `camera-timeout.json` |
| Delayed, missing, duplicate, stale payment | `payment-{delayed,missing,duplicate,stale}.json` |
| Stationary identification; noisy threshold | `stationary.json`, `noisy-threshold.json` |
| Obstruction before/during close | `obstruction-{before,during}-close.json` |
| Jam; missing upper/lower; contradictory limits | `jam.json`, `missing-{upper,lower}.json`, `contradictory-limits.json` |
| Partial-open startup | `partial-startup.json` |
| Reset during capture/payment | `reset-{capture,payment}.json` |

Fixture timings are explicit teaching defaults, not product deadlines beyond
the documented physical/controller contract. They work with default model
latencies and either tariff unit (no price is hardcoded). In particular,
`payment-stale` first settles an actual paid charge, completes exit, enters a
different plate and replays the retained old UUID while a distinct new missing
charge is pending.

Expected outcomes and recovery rules:

| Scenario group | Behavioral outcome |
|---|---|
| Normal / confidence retry | One authorized entry, physical beam passage and closure; a matching session exit opens only after matching payment. The confidence sequence consumes three capture IDs before opening |
| Low confidence / camera timeout | Three bounded attempts, closed/red assistance, no further stationary-vehicle retry loop |
| Delayed payment | Barrier remains closed while confirmation is pending; immutable idempotent retries do not create another charge; only the eventual matching result permits opening |
| Missing payment | Bounded same-charge retries then closed/red assistance, never opening |
| Duplicate / stale payment | A completed duplicate cannot reopen the lane; an earlier vehicle's paid UUID cannot authorize the different current pending charge |
| Stationary vehicle | Held point distance remains present; one capture/opening and no artificial absence or repeated capture during the nine-second fixture |
| Noisy threshold | Seeded ADC-code noise crosses the detection threshold; one recognition/opening, not repeated arrivals or repeated green |
| Obstruction before close | Barrier remains fully open/disabled while beam is held; passage deadline then requests assistance, without closing into the beam |
| Obstruction during close | Prompt stop at partial position; no automatic reversal or re-enabled drive after clearing the beam |
| Jam / missing upper | Motor stops at its firmware budget, red assistance, no green; missing-upper physics may reach/clamp at full opening but that does not substitute for valid feedback |
| Missing lower | Closing motion reaches/clamps at zero but missing feedback leads to deadline assistance, not a new identification cycle |
| Contradictory limits | Red assistance, no opening, latched until machine reset even if injected feedback is later restored |
| Partial startup | Assistance without homing or recognition; position remains partial and motor disabled |
| Reset during capture/payment | The actual pending response is cancelled by coherent same-machine reset; old-run commands reject and no old response authorizes the fresh run |

Clearing an equipment fault never writes firmware outputs or grants
authorization. The controller requirements permit closed-barrier assistance to
rearm only after the point vehicle departs beyond the clear threshold with
beam clear. Partial-position faults require intervention/coherent reset;
contradictory feedback and ADC faults are latched until reset. Reset is a
laboratory restart, not durable session/payment recovery.

Instructor validation can use separately maintained faulty firmware to prove
that a jammed motor left enabled fails `motor_deadline` and premature green
fails `green_authorization`. Those implementations are not supplied here;
scenarios cannot enable firmware mutations.

Host tests include strict malformed inputs, range/count/ordering bounds,
whitelist/schema consistency, negative behavioral controls, ring gaps/stale
events, adversarial UART-to-replay validation, and fake-official-runner status/
watchdog/cancellation cases. An optional `jsonschema` installation additionally
exercises `Draft202012Validator.check_schema` and all fixture documents; that
one test is explicitly skipped when the package is absent. Fake process tests are
**not Renode runtime evidence**. Retain actual controller, mutation and
reset results under the ignored build directory with exact input identities.
Parser/host tests alone do not establish runtime correctness.
