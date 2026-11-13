# Student specification: two-lane parking lot controller

## Status and scope

This specification describes the implemented laboratory defaults, not a tagged
classroom release or final course policy.
This repository provides the student starter and simulator, not a completed
controller or solved peripheral probes. Students design the solution.
The reviewed ZVB fixes are published and pinned at
`aed53b3cedad1e93d6a92837ce79d8cd7198d2af`; use the downloaded module as
described in [setup](setup.md). Project-owned source and documentation use
[Apache-2.0](../LICENSE). Existing runtime
evidence is macOS arm64 development evidence, including cached dependencies and
an existing SDK, **not a clean operating-system installation** or validation of
Linux/Windows. Read the [setup guide](setup.md) before starting.

Implement a Zephyr controller for `nucleo_u5a5zj_q` that runs a parking lot
with an entry lane and an exit lane at the same time, and up to
`CONFIG_PARKING_CAPACITY` parked vehicles (default 4). `student/Kconfig`
defines it (1..8) together with `CONFIG_PARKING_TARIFF_UNIT_MS`; the launcher's
`--capacity` option overrides the default. Use actual GPIO/EXTI, ADC4,
independent equipment UARTs and SPI transactions. Equipment models, not console messages or browser animations,
determine observed behavior. The supplied code is simulator infrastructure and
an inert starter, not a controller implementation or complete parking product.

The constants below are **implemented teaching defaults**, not user-selected
product policy or measured hardware specifications. Linked contracts provide
the detailed rules. Teaching duration, submission dates, grading weights and an
institutional AI-use policy have not been decided here; the course offering
must announce those separately. No week-by-week schedule is implied.

## Learning objectives

Demonstrate that you can:

- Configure Zephyr GPIO outputs and EXTI limit inputs; stop a commanded motor
  through firmware, including faults and deferred interrupt work.
- Acquire and scale distance through the real asynchronous ADC4 driver,
  distinguish presence from passage, and keep safety work responsive while
  conversions are pending or fail.
- Parse two independent fragmented UART byte streams using bounded buffers,
  deadlines, correlation, idempotent payment retries and stale-response rejection.
- Initialize and draw the ST7789V subset through Zephyr SPI and CS/D/C/reset,
  rather than printing display text to the console.
- Design a bounded state machine, integer tariff and reset-scoped session
  lifecycle; justify behavior with source-timestamped evidence.
- Test normal/fault behavior with public scenarios and explain model limitations.

## Supported development workflow

The launcher builds `student/` as the only application.
Shared public hardware is in `hardware/parking.overlay`,
`hardware/renode/parking.repl` and `hardware/dts/bindings/`.
Use the [setup guide](setup.md) for prerequisites and the
[release checklist](release-checklist.md) for classroom preparation.

The instructor supplies the reviewed environment and `ZVB_MODULE` (absolute
reviewed module path) and `RENODE_ROOT` (verified native executable directory).
Activate the local Python environment and work from the repository root.
The supported thin-wrapper entry point is:

```sh
python scripts/lab.py --module "$ZVB_MODULE" --renode "$RENODE_ROOT/renode" --output build/lab-01
```

The launcher builds the student app. The starter deliberately
does not implement recognition, payment, display or movement and is expected
to fail completed-controller fixtures. Use a new output directory for each
run. Instructor implementations are not included or selectable by the launcher;
their results would not be evidence about your implementation.

Open the printed local capability link on the same machine and keep it private.
Use scenario selection, start/pause/reset and typed equipment controls; inspect
GPIO/ADC/UART/SPI diagnostics and assessment results. Closing the browser does
not stop the engine; reconnect obtains current state. Stop the launcher with
Ctrl-C. No raw monitor, GDB, `west zvb-web` or developer
diagnostics belong in the normal student flow. A boot message is not proof
of controller correctness.

## Devices and ownership

Assignments come from the [hardware pin map](hardware-models.md#pin-map),
[physical model](physical-model.md#engineering-defaults-not-product-requirements)
and [equipment signals](device-protocols.md#display-and-signals).

| Device | Entry lane | Exit lane |
|---|---|---|
| Motor | PB12 enable; PB13 high opens, low closes | PB1 enable; PB2 high opens, low closes |
| Upper / lower limit | PC5 / PC6, EXTI IRQ 16 / 17 | PC2 / PC3, EXTI IRQ 13 / 14 |
| Independent passage/obstruction beam | PC8 | PC4 |
| Red / green / buzzer | PE2 / PE3 / PE4 | PD1 / PD2 / PD3 |
| Distance | ADC4 channel 17, PD13 | ADC4 channel 18, PB0 |
| Camera | USART3 PD8 TX / PD9 RX, IRQ 63 | USART2 PD5 TX / PD6 RX, IRQ 62 |
| Simulated terminal | none | LPUART1 PG7 TX / PG8 RX, IRQ 66 |
| Display | none | SPI1 PA5 SCK / PA6 MISO / PA7 MOSI; PD14 active-low CS, PD15 D/C, PE0 active-low reset |

Inputs are active high with pull down; outputs are active high. Buzzer outputs
are transitions, not host sound. ADC4 (IRQ 113) is shared; ADC1 is unsupported.
The diagnostic console is USART1 PA9 TX / PA10 RX, IRQ 61. In devicetree, the
`zephyr,user` properties carry the lane as a prefix, for example
`entry-enable-gpios` and `exit-red-gpios`.

Use the supplied board configuration. Do not replace driver reads with fabricated
results or host setters. Firmware owns authorization, outputs, retries, pricing
and sessions. Models own motion, sensors and UART replies. Scenarios own stimuli
and expectations. The browser only requests allowlisted equipment actions.

### Physical and ADC defaults

- Barrier position: 0..90000 millidegrees, speed 45000 millidegrees/s
  (450 per 10 ms tick). Reset is closed, disabled, direction low.
  After a drive, a sampled direction change inserts ten zero-speed ticks
  (100 ms); another change restarts that wait.
- Vehicle position: +6000 mm approach to -2000 mm passed, initially stationary.
  Velocity: -2000..2000 mm/s in 100 mm/s steps; negative approaches/passes.
  A stationary point remains present indefinitely.
- Distance: `min(abs(vehicle_mm), 6000)`. Raw code:
  `distance_mm * 4095 / 6000`, truncated/clipped to 0..4095.
  Thus 0 mm → 0, 3000 mm → 2047, 6000 mm → 4095.
- Software-triggered ADC4, one channel, 12-bit, gain 1, internal-reference
  metadata 3300 mV. Zephyr scaling is `raw * 3300 / 4096`:
  0 → 0, 2048 → 1650, 4095 → 3299 mV.
  See [ADC4 scaling](hardware-models.md#injection-and-scaling).
- Physics/sensors run at 100 Hz, holding the latest sample between ticks.
  Optional seeded noise has amplitude 0..64 raw codes; reset restores
  noise off, seed 1, vehicle +6000 mm and jam/obstruction off.
- Beam is independent of distance/motion. Passage does not toggle it
  automatically. No collision/body geometry prevents passing a closed barrier.
- Jam holds position but leaves enable unchanged. Endpoints clamp position but
  leave enable unchanged. Model deadline diagnostics do not stop the motor:
  **firmware must stop it**.

## Controller and safety requirements

Match the [controller behavior contract](device-protocols.md#controller-behavior-tariff-and-passage)
and [scenario outcomes](scenarios.md#initial-fixture-coverage-and-validation-boundary),
not prescribed symbols or source organization.

1. Start disabled, red on, green off. IDLE without a lower limit requests
   assistance, not automatic homing. Simultaneous upper/lower feedback latches
   assistance until coherent reset.
2. At a closed lower limit, fresh raw distance <683 (about 1000 mm) starts
   recognition. Accept a valid plate and confidence **>=85** for the current
   capture ID. Allow three attempts, a new ID each time, each with a 1000 ms
   complete-response deadline. Low confidence/timeouts consume attempts;
   exhaustion requests assistance, not infinite retries.
3. An accepted plate without a session may enter while a spot is free; a
   full lot or an already parked plate requests assistance. On the exit lane,
   only a plate with an active session may pay and leave; any other plate
   requests assistance. Both lanes run concurrently, and one lane's fault or
   waiting state must not stall the other's safety servicing.
   During identification/payment, fresh code >1024 (about 1500 mm) abandons
   the attempt.
4. Exit needs an immutable integer charge and exact current PAID confirmation
   before opening. Missing, stale, malformed, cancelled or mismatched replies
   cannot open. Invalidate active authorization before cancellation/abandonment.
5. Stop through actual limit feedback. OPEN and CLOSE each have a 2500 ms
   firmware budget. Do not rely on endpoint clamping. Green is permitted only
   after this authorized opening reaches the upper limit **with motor disabled**.
6. After opening, require beam active then inactive with fresh distance >1024.
   Only completed passage creates an entry session or removes a paid exit
   session; payment or opening alone does neither. Passage budget: 10000 ms.
7. Green off/red on before closing; beam must be clear. Obstruction during
   closing must disable the motor within 10 ms. Do not automatically reverse
   or re-enable after a partial-position fault, even if the beam clears.
8. Assistance stops the motor, clears green, asserts red and pulses buzzer
   100 ms. Closed-barrier assistance rearms only after distance >1024 with
   clear beam. Partial-position faults need intervention/reset; contradictory
   limits and ADC faults remain latched until reset.

ADC buffers, sequence and signal must outlive arbitrarily late completion.
Allow one outstanding request, check completion nonblockingly and do not
resubmit while it remains owned. Completion must be observed **before**
submission uptime +100 ms. Submission/result/invalid-sample/deadline faults
latch assistance until reset; late IRQ cannot rearm acquisition/authorization.
Continue motor/beam/deadline/buzzer work while pending; use fresh samples for
distance-dependent transitions and authorization. Choose and explain your own
implementation of these ownership and liveness requirements.

Deadlines use **Renode virtual time**, not browser time or host sleeps.
Pause freezes physics, protocol delivery and firmware time. Reset clears
firmware sessions, request namespaces, equipment replies and traces coherently;
it is a lab restart, not durable storage or a refund. Run ticks restart at
zero; absolute machine source timestamps may continue.

## UART protocol v1

Follow the full [equipment protocol](device-protocols.md#ownership-and-wire-framing).
Both channels use case-sensitive printable ASCII bodies. Camera uses CRLF;
payment uses LF and exactly one space between fields, with no CR.
No escaping, empty fields or extra whitespace are accepted.
Maximum frame: **128 bytes including its terminator**. Discard malformed or
overlong input through LF; never execute a valid-looking prefix.
Equipment partial requests have a 100-tick/1000 ms assembly deadline, then
discard the suffix through LF. Keep independent bounded UART parsers.

```text
AT\r\n
OK\r\n
AT+CAPTURE=<capture_id>\r\n
OK\r\n
+PLATE:<capture_id>,"<plate>",<confidence>\r\n
P1 CHARGE <request_uuid> <plate> <amount> BRL\n
P1 CANCEL <request_uuid>\n
P1 RESULT <request_uuid> PAID <amount> BRL\n
P1 RESULT <request_uuid> CANCELLED <amount> BRL\n
```

Here `\r\n` means CR followed by LF, and `\n` means LF.
The camera is a course-defined AT extension, not a vendor standard.
`OK` acknowledges acceptance, not a recognized plate. Only a matching valid
`+PLATE` result can authorize recognition. Echo is disabled; acknowledgments
and errors do not restart deadlines. Missing camera results can still have `OK`.

- Capture ID: canonical decimal 1..999999999, no leading zero. Plate:
  `[A-Z]{3}[0-9][A-Z0-9][0-9]{2}`. Confidence: canonical 0..100.
  Default: `ABC1D23`, confidence 93, latency 10 ticks, fragments at most
  8 bytes, separated by one tick. Repeated ID returns its original result;
  a new recognition attempt uses a fresh ID.
- UUID: lowercase hexadecimal `8-4-4-4-12`, exactly 36 characters.
  Amount: canonical integer **centavos**, 1..1000000; currency exactly BRL.
  Same UUID and immutable plate/amount/currency is one idempotent charge;
  changed fields conflict. At most three total sends, 1000 ms deadlines,
  preserving UUID/price. Never invent another UUID because delivery is uncertain.
- Default automatic payment: simulated PAID after 10 ticks, at most 8 bytes
  per fragment, one tick apart. Manual browser mode suppresses automatic
  replies; Pay requests the retained reply through real LPUART1 bytes.
  It does not open the barrier directly.
- Settlement commits on the first response byte. Before it, cancellation
  removes pending frames/freezes CANCELLED; after it, cancellation cannot
  truncate transmission or undo PAID. Correlation is not authentication.
- Camera errors are `ERROR\r\n`; payment errors remain
  `P1 ERROR <code>\n` with FRAME/TIMEOUT/CAPACITY/CONFLICT/UNKNOWN.
  No correlation ID means they are diagnostics,
  not proof that an uncertain charge did not execute.
- Equipment retains 64 captures and 64 UUIDs per reset, no eviction;
  at most 32 response frames across both UARTs. Capacity/queue failure cannot
  authorize opening. Reset reclaims the caches.

This is functional UART byte-stream/driver behavior, not bit-level baud timing,
electrical accuracy, DMA or general interrupt-driven UART application support.

## Display and pricing

Use the [display subset](hardware-models.md#display)
and [equipment contract](device-protocols.md#display-and-signals):
240×320 native row-major RGB565, high byte first, four-wire 8-bit SPI.
Supported commands: `01` SWRESET, `11` SLPOUT, `28` DISPOFF, `29` DISPON,
`3A` COLMOD (`55`), `36` MADCTL (`00`), `2A` CASET, `2B` RASET, `2C` RAMWR.
Use valid windows. Reference startup asserts reset 10 ms, waits 120 ms after
release, 150 ms after SWRESET and 120 ms after SLPOUT. These waits are in
firmware, not timing-enforced by the model. Rotation, readback, scrolling, TE
and full Zephyr display-driver compatibility are not supported claims.

During exit payment display the recognized plate, `BRL <reais>.<two centavos digits>`,
`SIMULATED PAYMENT` and `NOT PIX`. Pixels must come from actual SPI.
Never present a real payment QR/request.

The [assignment tariff](device-protocols.md#controller-behavior-tariff-and-passage)
is an implemented teaching default, **not final commercial policy**:

```text
units = max(1, ceil(elapsed_ms / unit_ms))
amount_centavos = 200 + (units - 1) * 100
```

Default `unit_ms`: 60000. Explicit finite tests may use 1000
(`CONFIG_PARKING_TARIFF_UNIT_MS`, range 1000..60000). Time starts at completed
entry passage. Freeze at the first exit charge calculation, after rendering
the recognized plate and before rendering amount/status and sending CHARGE.
SPI/UART time and retries do not recalculate the price. Minimum: 200 centavos.
At the default unit, 60000 ms costs 200 and 60001 ms costs 300.
Above 9999 units request assistance, not overflow.

## Deliverables and assessment

Submit in the form announced by the instructor:

1. Student source/configuration and reproducible build description, exact
   source/dependency identities and any tariff override.
2. State/transition design, pin/interface map, bounded parsers/retries, ADC
   ownership explanation, tariff examples and fault/recovery reasoning.
3. Public-scenario results with unmodified inputs, source run IDs/virtual
   timestamps and actual GPIO/UART/ADC/SPI evidence. Include normal entry/exit
   and a safety fault; screenshots alone are insufficient. Retain failures.
4. Known limitations/test gaps, attribution and any permitted AI-use record.

Public `tests/scenarios/` fixtures cover normal entry/exit, confidence
62→79→93, low confidence/timeouts, delayed/missing/duplicate/stale payment,
stationary/noisy distance, obstruction, jam, missing/contradictory limits,
partial startup and pending-response resets. Public practice contracts are
public practice material, not hidden instructor assessment. Run your own
firmware; a different implementation's results are not evidence about your submission.

Assessment observes peripherals, not required function names, logs or a copied
architecture. It checks every green transition, continuous motor/obstruction
safety and timed checkpoints. The 2500 ms motion budget has one 10 ms servicing
tolerance (failure above 2510 ms), not a new allowance per test step.
Trace gaps/transport errors are harness failures, not passes or automatically
student faults. Owner-local traces cannot resolve every same-timestamp
cross-owner ordering; see [assessment limits](scenarios.md#continuous-safety-and-every-green-authorization).

Proposed dimensions: functional correctness, safety/fault behavior, protocol
robustness, evidence and explanation. **No weights or automatic grading policy
are approved here.** Publish course rubric, collaboration/AI and resubmission
rules before assessment.

### Proposed AI-use baseline, pending course approval

If the instructor permits AI assistance, record tool/model, task, affected
files/ideas and how each contribution was checked. Attribute reused code and
licenses. Be able to explain and modify all submitted control/parser code.
Do not fabricate results, hide failures, submit instructor firmware as your
own or upload private material/credentials to external services.
AI output is not correctness evidence: run the same device-based checks.
This proposal neither replaces institutional academic-integrity policy nor
implies that AI use has already been authorized.

## Trust and model limitations

Run only trusted local firmware/models/prepared machine inputs: they can execute
local code. **This environment is not a sandbox for submissions.** The normal
browser boundary uses protected loopback HTTP/SSE without raw monitor/GDB/Robot.
Keep capabilities/cookies out of evidence. Developer official Robot may bind
wildcard TCP 49152 and is not normal student launch.

No real payment/image recognition, durable sessions, authentication, vehicle
continuity proof, multi-lane concurrency, physical safety certification,
collision simulation, ADC1/DMA, full RCC/power/clock/electrical accuracy or
general MCU/display compatibility is claimed. Read the
[ADC limits](hardware-models.md#unsupported-adc-behavior),
[IO limits](hardware-models.md#uart-and-spi-controller-limitations) and
[browser boundary](browser.md#launch-and-trust-boundary).
