# Device protocols and controller requirements

This is a course simulation contract, not a camera/payment vendor protocol,
real payment, image recognition, safety certification, or a complete student
specification. The retained board is `nucleo_u5a5zj_q`, ADC4 channel 18.
The [physical/time contract](physical-model.md), limited U5
ADC4/EXTI/RCC support, and native ST7789V subset still apply.
The [scenario runner](scenarios.md) and [browser](browser.md) exercise these
same device interfaces. Debugger support remains deferred.

These are external-device contracts and observable requirements. They do not
prescribe firmware function names, thread structure, parser implementation or a
state-machine architecture. Students design those components.

## Ownership and wire framing

`models/ParkingProtocols.cs` connects directly to MCU USART2 (camera) and
LPUART1 (payment). USART1 remains the console. Firmware bytes emitted through
Zephyr UART become requests; equipment replies enter the same actual MCU UART
receive path. Neither firmware logs nor simulator payment actions set the motor.
No extra terminal transaction UUID is introduced: the request UUID already
identifies the immutable simulated charge and its result.

Both protocols use printable ASCII bytes 0x20..0x7e, with **LF (0x0a)** as the
only frame delimiter. Maximum frame size is **128 bytes including LF**.
There is no CR, escaping, JSON, image, binary payload, optional whitespace,
leading/trailing whitespace or empty field. Exactly one space separates fields.
All commands are case-sensitive. Encoding or length errors discard through LF;
no prefix of a malformed request can execute. The model emits one `ERROR FRAME`
after that LF. A partial request must finish within **100 ticks / 1000 ms**
from its first byte, or the model emits `ERROR TIMEOUT` and discards the suffix
through the next LF without executing it. The following complete frame can
resynchronize. Idle UARTs do not time out.

Each independent UART parser retains at most 127 bytes. Models retain at most
64 capture IDs and 64 charge UUIDs per reset-scoped run, with **no eviction**:
new IDs receive `ERROR CAPACITY` when full, while retained IDs remain
idempotent. There are at most 32 pending response frames across both UARTs;
each is at most 128 bytes. If full, increment `Errors`, emit `queue_full`,
and drop the new response without undoing an already accepted operation.
This bounded failure may look like a timeout to firmware; retry the **same**
payment UUID, never invent a new charge because delivery is uncertain.
The single-session assignment stays below those limits. Reset is the
only reclamation mechanism for the ID caches.

## Camera C1

```text
C1 CAP <capture_id>\n
C1 RESULT <capture_id> <plate> <confidence>\n
C1 ERROR FRAME\n
C1 ERROR TIMEOUT\n
C1 ERROR CAPACITY\n
```

- `capture_id`: canonical decimal integer 1..999999999, no leading zero.
- `plate`: exactly `[A-Z]{3}[0-9][A-Z0-9][0-9]{2}`, covering the course's legacy
  and Mercosur-like seven-character labels. No hyphen, Unicode or spaces.
  This is syntax validation, not registration validation.
- `confidence`: canonical decimal integer 0..100. **85 is accepted**.
- A repeated ID returns the originally captured plate/confidence, even after
  configuration changes; a new ID takes the next configured confidence.
- Default plate `ABC1D23`, confidence 93, latency 10 ticks (100 ms), fragments
  of at most 8 bytes, separated by one 10 ms tick.
- Trusted paused configuration:
  `ConfigureCamera(plate, commaSeparatedConfidences, delayTicks, fragmentBytes, missing)`.
  Validate the entire configuration before mutation. Confidence sequences have
  1..16 values; the last repeats. Latency is 1..3000 ticks, fragments 1..128
  bytes. Missing mode captures/cache-correlates but emits no reply.
- There is no image processing or linkage from vehicle position to plate.
  The scenario/operator configures recognition labels; firmware learns the
  label **only** through the camera UART.

Permit **three capture attempts**, each with a fresh ID and
1000 ms virtual-time deadline including complete fragmented response delivery.
Low confidence immediately starts the next attempt. Timeout starts the next
attempt. Expired IDs, malformed responses, impossible confidence and invalid
plates are ignored, never accepted opportunistically. After three attempts,
stop and request assistance; no repeated recognition loop while the same
vehicle remains at the identification point.

## Payment P1

```text
P1 CHARGE <request_uuid> <plate> <amount> BRL\n
P1 CANCEL <request_uuid>\n
P1 RESULT <request_uuid> PAID <amount> BRL\n
P1 RESULT <request_uuid> CANCELLED <amount> BRL\n
P1 ERROR FRAME\n
P1 ERROR TIMEOUT\n
P1 ERROR CAPACITY\n
P1 ERROR CONFLICT\n
P1 ERROR UNKNOWN\n
```

- UUID wire form: lowercase hex `8-4-4-4-12` groups, exactly 36 characters.
  The parser does not infer authentication or cryptographic properties from it.
- `amount`: **integer centavos**, canonical decimal 1..1000000, no sign,
  decimal separator, floating point, leading zero or currency conversion.
  `BRL` is required and case-sensitive. Plate syntax is identical to C1.
- A UUID permanently binds the plate, amount and BRL currency within the run.
  Same UUID + same immutable fields is an idempotent retry, **one charge**.
  While a reply is pending, a retry does not enqueue a second reply; otherwise
  it returns the retained result. A changed plate/amount is `CONFLICT`.
  Invalid syntax/currency is `FRAME`, not another charge.
- A new charge's configured outcome is frozen. Default outcome is simulated
  `PAID` after 10 ticks, in fragments of at most 8 bytes, one tick apart.
  `ConfigurePayment(delayTicks, fragmentBytes, result, missing)` is trusted and
  paused; result is `PAID`, `CANCELLED`, or the deliberate `MALFORMED` fault.
  The latter emits `P1 RESULT <uuid> PAID broken BRL\n`, not valid success.
  Changing configuration does not rewrite retained charge outcomes.
- Settlement commits **when the first response byte is transmitted**. Before
  that point, `CANCEL` removes pending frames for the UUID and freezes
  `CANCELLED`; a cancellation reply is eligible next tick, subject to FIFO
  ordering behind other frames. Once transmission has
  begun, cancellation cannot truncate the frame or undo a paid outcome.
  Cancelling a retained cancelled charge remains cancelled; unknown UUID is
  `UNKNOWN`. This is simulated cancellation, not a real financial refund.
- Missing mode accepts/cache-binds the charge but suppresses automatic replies.
  Delayed/missing confirmations do not authorize opening. Correlation alone
  does not establish who paid, driver identity, or vehicle continuity.

Use one new UUID per exit attempt and send that exact immutable charge at most
**three times**, with 1000 ms deadlines. Freeze the amount before the first send;
never recalculate it on a retry. On timeout, departure or cancellation,
invalidate the active UUID **before** sending `CANCEL`, then request assistance.
Accept `PAID` only while awaiting the current result with the exact active UUID,
amount and currency. Malformed, stale and
duplicate responses cannot authorize a later attempt. No response causes a
direct model motor write.

Request UUIDs must distinguish exit attempts within a coherent machine/equipment
run; choose an identifier strategy rather than treating a repeated amount or
plate as a transaction ID. This simulation does not provide global or durable
identity. Reset clears firmware sessions/request state, terminal
UART FIFOs, parser fragments, terminal charges, scheduled replies and run IDs
together. Old-run host commands reject. Never attach an external payment
service or inject queued pre-reset tester bytes into this contract: cancellation
cannot cover queues outside the equipment owner. A durable integration needs
durable session/request namespaces and authentication, not this test counter.

## Scheduling, typed actions, observations and reset

Models reuse ZVB's `EquipmentTimeline` unchanged. Each registered owner forwards
reset exactly once; the model's private bounded reply queue advances only in
its 10 ms virtual-time integration callback. No host sleep or console drain
delivers protocol data. Accepted host commands execute FIFO before that owner's
integration. Responses due at an integration tick emit at most one fragment per
UART, in queued frame order; frames cannot interleave on a UART. Camera and
payment progress independently. A response cannot start earlier than the next
tick. Queueing delay and fragmentation count against firmware deadlines.
Across owners and firmware, Renode's serialized callback order is authoritative,
as in the physical model; this is not a global atomic scheduler.

Trusted paused actions use the same `Submit(v, run, id, atTick, signal, value)`
envelope. `payment.replay = 1` requests another response for the latest retained
charge; `payment.cancel = 1` applies the cancellation semantics above.
No charge yet means no effect. They only enqueue bytes to **LPUART1**.
`ReplayPayment(retainedCompletedUuid, delayTicks)` is a paused test/fault helper
for reproducing a specific stale result (latency 1..3000 ticks), not an arbitrary
frame injection API or a browser endpoint. It requires a retained settled ID.

`Trace` reuses source virtual timestamps, sequence/run IDs and the bounded
8192-event ring. UART TX bytes are `commanded`, RX bytes `sensor`; camera
confidence, accepted charge centavos and completed replies are `physical`.
Errors/queue overflow are `diagnostic`. Byte traces permit exact frame and
fragment-timing reconstruction, independently of firmware logs.
Protocol `ERROR` frames deliberately have no correlation ID: the failed frame
may not contain a valid one. They are diagnostics, not authorization or proof
that an uncertain charge did not execute. They must not bypass the controller's
bounded deadlines or authorization checks.
Paused `Snapshot` exports counts, pending work, red/green/buzzer and dropped
observations. `Pending` includes host actions and reply frames, not UART input
parser bytes or MCU FIFOs. `LastPayment` and `LastUuid` expose the latest
accepted immutable charge for trusted diagnosis.

Pause freezes parser timeouts, latency and fragments. Reset restores defaults,
clears all caches, fragments, pending work and observations, and starts new run
IDs. Configuration is intentionally **not** persistent across reset.
Model latency/assembly deadlines are tick-quantized: enqueue at the current
`Tick + delayTicks`, not host elapsed time or a separate sub-tick timer. Thus
10 ticks means nominal 100 ms with up to one tick of phase quantization relative
to the request's LF. Source timestamps and serialized callback order resolve
equal-time boundaries. A fragment's bytes enter the UART at one callback
timestamp; bit-level baud/electrical timing is not modeled.
Official 1.17 UART testers are cached by UART object: clear completed reports
and assert fresh timestamps after each reset; merely creating a new tester
does not isolate old output.

## Display and signals

The actual SPI1 model is the only display source. Firmware must issue hardware
reset then SWRESET, SLPOUT, RGB565 COLMOD=0x55, native MADCTL=0, DISPON,
CASET/RASET/RAMWR, with documented reset/wake delays. Students choose their own
font/rendering implementation using real chip-select/D/C/reset signals.
Render the camera plate, `BRL <reais>.<centavos>`, **SIMULATED PAYMENT**, and **NOT PIX**.
There is no QR or real PIX request.

Supported display commands remain `01`, `11`, `28`, `29`, `3A`, `36`, `2A`,
`2B`, `2C`. Native orientation is 240×320, row-major RGB565, high byte first.
Unsupported commands/modes, incomplete parameters/pixels, invalid windows,
data outside selection/reset and RAMWR before initialization increment `Errors`.
SWRESET retains frame RAM/format/orientation/windows; hardware reset clears
controller state but retains frame RAM; machine reset clears deterministic RAM.
No TE, scrolling, readback, DMA, rotation or general display-driver claim.

Paused `display.PixelState` exports the fixed framebuffer as **307200 uppercase
hex characters**, four characters per pixel, separate from `Snapshot` metadata
(`v`, dimensions, format, initialization, writes, errors, dropped command count).
`GetPixel(x,y)` remains compatible. The recent-command history is bounded at
128 commands with `DroppedCommands`; framebuffer export is not a UI or stream.

Red PD1, green PD2 and buzzer PD3 outputs are observable transitions in the
protocol timeline, not host audio. Red is on while identifying/waiting/closed;
green is on only after authorized opening reaches the upper limit with motor
off. Assistance produces a 100 ms buzzer pulse and red. Motor/limits/beam stay
on the [hardware pin map](hardware-models.md#pin-map); D/C PD15, CS PD14,
reset PE0 active low.

## Controller behavior, tariff and passage

Implement one lane and at most **one active session**.
The first recognized vehicle enters; while that session is active,
only its matching plate can attempt exit. A different plate requests assistance,
not a new entry or someone else's payment. Multi-session capacity, durable
storage and configurable simultaneous entry/exit lanes are later work.

1. ADC code <683 (about 1000 mm) at a closed lower limit triggers recognition.
   Three capture attempts as above. An accepted first entry opens through
   PB2/PB1 only. A known-session exit first displays and requests payment.
2. While identifying/waiting for payment, code >1024 (about 1500 mm) abandons
   that attempt and invalidates its request. This is coarse point-presence,
   not proof that nobody exchanged vehicles at the same physical location.
3. Limit EXTI callbacks defer GPIO shutdown to Zephyr work context. The main
   loop additionally enforces 2500 ms opening/closing deadlines. No model
   silently clears a guest motor command.
4. After full opening, require the independent passage/obstruction PC4 sensor
   to be observed active, then inactive with distance code >1024. Only then
   create an entry session or remove a paid exit session. A paid/open barrier
   alone does not remove a session. Passage budget is 10000 ms.
5. Turn green off, red on, close through GPIO, and wait for actual lower/motor
   off before accepting another identification. Obstruction during closing,
   movement/ADC failure, or deadline violation stops and requests assistance.
   Partial-open faults require intervention/reset; no automatic unsafe recovery.
6. Assistance at a closed barrier rearms only after distance >1024 with clear
   beam. A stationary failed vehicle is not retried indefinitely.

Startup/feedback faults have explicit outcomes: a ready controller without the lower limit
requests assistance, never automatic homing or
recognition. Simultaneous upper/lower feedback latches assistance until
coherent machine reset, including if a scenario later clears that injected
fault. Buzzer servicing continues while latched. A missing destination limit
does not stop physical motion at a different position; the normal 2500 ms
firmware deadline must stop the guest motor and request assistance. An
obstruction held before closing prevents the passage-clear transition; the
10000 ms passage deadline eventually requests assistance. An obstruction
during closing stops the motor and requests assistance without reversal.
No fault action is itself authorization or permission for automatic recovery.

ADC acquisition uses one persistent asynchronous request at a time and a
nonblocking completion check. Pending acquisition does not stop motor/beam/
deadline/buzzer servicing, but only a fresh sample permits distance-dependent
transitions and UART authorization parsing. A missing/failed completion or one
observed at/after its 100 ms deadline latches an ADC fault until machine reset;
late completion cannot rearm a read or recover authorization. Retained
buffer/sequence/signal storage remains valid throughout. This single-owner
policy does not assume an ADC cancellation API. This is an ownership/liveness
requirement, not a supplied acquisition implementation.

Tariff default is **200 centavos for the first started 60000 ms of virtual
parking time**, plus **100 per additional started unit**, minimum 200.
Time starts at completed entry passage. Freeze occurs when firmware computes
the first exit charge, after rendering the recognized plate and before
rendering amount/status and sending `CHARGE`; those SPI/serial operations do
not recalculate the amount. Retries preserve that same charge.
Integer arithmetic uses `max(1, ceil(elapsed_ms/unit_ms))`. Above 9999 units,
request assistance rather than overflow/exceed the terminal's 1000000 limit.
`CONFIG_PARKING_TARIFF_UNIT_MS` defaults to 60000; tests explicitly build 1000
for finite checks. This is a teaching tariff, not final product policy.
No session is persisted across reset; reset is a lab restart, not paid storage.

### Reusing the point vehicle for the next approach

`vehicle.next = 1` is a physical typed command.
At execution, require `Vehicle == -2000`, vehicle speed 0, barrier position 0
and guest enable false **before any mutation**. Otherwise throw: the reviewed
generic timeline emits `failed`, retains the command ID, continues FIFO and
leaves the scene untouched. On success, despawn the passed point and introduce
the next point at +6000 mm; subsequent approach/passage still uses velocity,
ADC and the independent beam. This gives the existing one-vehicle model a
bounded lane lifecycle without an arbitrary position/plate/barrier setter or
a new scenario framework. Plate identity still comes only from camera UART.

## Boundaries

Use the published tooling revision pinned by the manifest. Correlation is not
authentication. The test-only official Robot control service may listen on
wildcard TCP 49152: headless is **not listener-free** or network containment.
Use a trusted isolated host with inbound filtering for that developer path.
The normal student [browser and headless launcher](setup.md) uses private
runtime pipes instead. The legacy dashboard and supported debugging stay disabled.
