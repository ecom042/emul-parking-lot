# Physical simulation and virtual time

## Scope

`models/ParkingPhysical.cs` is one lane with one barrier, one vehicle, an
independent obstruction input, and one continuous distance source for the
existing ADC4 subset. Student firmware uses real Zephyr GPIO/EXTI and
asynchronous ADC drivers on **nucleo_u5a5zj_q**. The model provides an
environment, not a parking-controller implementation or safety certification.

The [hardware-model limits](hardware-models.md) apply: ADC1, full
clock-tree/electrical accuracy and general driver support are not established.
See [device protocols](device-protocols.md) for camera/payment behavior and
[the browser interface](browser.md) for local interactive control.

## Engineering defaults, not product requirements

These constants deliberately define reproducible teaching abstractions, not
measurements, physical safety claims, sensor vendor calibration, or approved
product deadlines. Change the source contract and tests together if a later
course/product decision selects other values.

| Quantity | Contract |
|---|---|
| Motor enable | PB1, active high; false produces zero speed |
| Direction | PB2 high opens, low closes; alone cannot move the barrier |
| Barrier position | Integer millidegrees, 0 closed/down to 90000 fully open |
| Free-running speed | 45000 millidegrees/s; 450 millidegrees per 10 ms tick; ideal constant velocity, no inertia |
| Upper/lower outputs | PC2 / PC3 active high, pull down; asserted exactly at 90000 / 0; no bounce or hysteresis. Missing/contradictory feedback faults (below) are independent of physical position |
| Startup/reset | Down, enable/direction false, lower asserted, upper false; vehicle +6000 mm; obstruction/jam/noise off, seed 1 |
| Sensor settling after reset | Held GPIO levels are re-driven at each 10 ms sample, without synthetic source edges; receivers reset independently, so allow the first sample before asserting guest inputs |
| Reversal | After any drive, a sampled direction transition causes ten complete zero-speed ticks (100 ms); a further transition restarts this wait; disable stops immediately at the next tick |
| Deadline | After >250 consecutive enabled samples (2.5 s budget), emit one `enabled_deadline`; do **not** change firmware GPIOs |
| Jam | Holds position and zero speed; leaves enable/direction untouched. Clearing jam permits movement from retained position |
| Endpoint with enable on | Clamp position; report `endpoint_enabled` every tick; leave firmware enable asserted |
| Vehicle | Signed position mm relative to sensor/barrier plane; +6000 approach to 0 plane to -2000 passed; motion saturates at those bounds |
| Vehicle velocity | Signed mm/s, -2000..2000 in 100 mm/s steps, initially 0. Negative approaches/passes; can remain stationary indefinitely |
| Obstruction | PC4 active high; independent scheduled boolean, **not** inferred from distance, vehicle or barrier |
| Distance | `min(abs(vehicle_mm), 6000)` mm; this simple point vehicle is not collision/body geometry |
| ADC calibration | 0 mm → 0, 3000 mm → 2047, 6000 mm → 4095; integer `distance_mm * 4095 / 6000`, clipped to 0..4095 |
| ADC electrical interpretation | Ideal 12-bit full-scale code on ADC4 channel 18/PB0, Zephyr reference metadata 3300 mV; guest scaling remains `raw * 3300 / 4096` |
| Noise | Uniform integer code perturbation in `[-amplitude,+amplitude]`, amplitude 0..64; LCG `state = state*1664525 + 1013904223` modulo 2^32, then modulo `(2*amplitude+1)`; positive signed 32-bit seed |
| Update rates | Physics, vehicle and sensors 100 Hz. One PRNG advance/tick even with amplitude zero. ADC holds the most recent sensor sample between ticks |

The distance model is a course calibration, not an ultrasonic/infrared transfer
curve. Obstruction reports a sensor input; it does not force a motor stop.
The firmware owns stop/recovery decisions. The model does not prevent a vehicle
passing a closed barrier; collision and realistic driver behavior are outside
this model. `enabled_deadline`, `jam_enabled`, and `endpoint_enabled` are
observations, not automatic repairs or assessment verdicts.

## Time, ordering, reset

The generic attached tooling extension is documented in its
`docs/equipment-v1.md`. `equipment_timeline: true` loads it through shared
machine assembly for both headless and interactive runs; the course model loads
through REPL `preinit`, like the other peripheral models. No runtime installation edits
or course rules in ZVB are needed.

One Renode `LimitTimer` owns the 10 ms schedule. No host timer, UART console log,
stdout hook, browser delivery or wall-clock sleep advances physical state.
Every tick executes:

1. Due validated commands, FIFO for equal execution tick.
2. Sample actual motor enable/direction GPIO levels and integrate barrier.
3. Integrate vehicle and draw the seeded sensor noise.
4. Drive limit/obstruction inputs and publish held distance ADC code.

GPIO writes are observed immediately in the `commanded` trace category and
sampled at the next equipment callback. At the same machine timestamp, Renode's
serialized callback order is authoritative: a write before that sample affects
it; a write after it affects the next sample. Sequence numbers expose that
ordering, including synchronous sensor→EXTI→firmware reactions. This is fixed-step
functional simulation, not a promise of sub-tick mechanical response or MCU
cycle-accurate sensor timing. Scheduled future device responses use the same
FIFO dispatch-before-integration stage, not a second wall-time queue.

Pause freezes physics, ticks, PRNG and pending actions. Resume continues the
same timer/run. Reset/reload is performed while paused using the generated
`reset.resc`: machine peripheral/CPU reset, then reload the same ELF's entry and
vector setup. Physical reset clears its state, seed, reversal/deadline counters,
pending actions, accepted IDs, trace and sequence. ADC reset clears registers,
conversion timer/IRQ/FIFOs; the physical `SampleProvider` **connection** persists
like GPIO wiring, while its sample state resets with its owning model.
The physical provider and standalone `FeedSample` FIFO are mutually exclusive,
so test queues cannot silently override physics.

Run IDs change across reset and across new machines in one Renode process.
Commands from an old run are rejected. The process/connection scopes the integer
ID namespace; reconnection must discard old bindings/commands. Renode save/load
is not a supported continuation/replay mechanism.
Machine elapsed time can continue across reset; run-relative ticks restart at 0.

Do not queue old UART tester writes and expect this scheduler to cancel them:
it owns **only its own queue**. Tests deliver guest commands before reset,
clear completed terminal reports, and require post-reset virtual UART timestamps.
Official 1.17 caches terminal testers by UART object; creating another tester
does not isolate old or partial lines.

## Command/event bridge

The minimal v1 boundary is the typed `Submit(version, run, id, atTick, signal,
value)` method plus paused `Trace`/`Snapshot` reads. ZVB's Python
`equipment.submit` validates the envelope for trusted host adapters; the model
also validates run, IDs, time, queue bounds and allowlisted payloads. No submitted
command can set barrier position, motor outputs, limits or ADC codes.

| Signal | Value/units |
|---|---|
| `fault.jam` | 0/1 |
| `fault.limits` | 0 normal; 1 suppress upper; 2 suppress lower; 3 force both high. Fault changes sensor feedback only, never position or motor GPIO |
| `sensor.obstruction` | 0/1 |
| `vehicle.speed` | mm/s, range/step above |
| `vehicle.next` | Value 1; only after passed at -2000 mm, speed 0, barrier down and disabled; introduce next point at +6000 mm |
| `sensor.noise` | amplitude in ADC codes |
| `sensor.seed` | positive signed 32-bit PRNG seed |

`at_tick` is an absolute future run tick (10 ms), maximum one hour ahead.
Acknowledgments report acceptance and exact execution tick; execution emits a
separate event containing the original command ID/payload. Duplicate IDs reject.
Interactive adapters must pause and invoke this typed boundary on Renode's
serialized control context, then resume; headless tests use the identical method
before finite `RunFor` steps. Running submissions reject. This boundary does not
accept raw monitor text or arbitrary method names. The browser uses a
[separate validated adapter](browser.md#student-facing-api).
The privileged Robot harness invokes the public method through its existing
test transport; that harness is not a student control endpoint.

The model also provides trusted `ConfigureInitial(positionMillidegrees,
vehicleMillimetres, seed)` for paused tick-zero scene setup with motor disabled.
It validates all arguments before mutation: position 0..90000, vehicle
-2000..6000, seed 1..2147483647. This is not an accepted scheduled command or a
running position setter. Tick zero is a model-clock condition, not proof that
the CPU has never executed within its first 10 ms; the headless runner invokes
it before its first `RunFor`. Reset discards this configuration and restores
the standard closed/far/seed-1 scene and normal limit feedback.

`vehicle.next` checks scene preconditions again **at execution**, before
mutation. A changed/ineligible scene produces a generic `failed` command event,
not `executed`; the command ID remains consumed and other FIFO work continues.
This is a lifecycle action, not arbitrary position or plate injection. See the
[device contract](device-protocols.md#reusing-the-point-vehicle-for-the-next-approach).

Event envelope:

```json
{"v":1,"run":5,"seq":22,"time_us":100000,"tick":10,
 "category":"physical","device":"barrier","signal":"position_mdeg","value":4500}
```

Categories distinguish `command`, `commanded`, `physical`, `sensor`,
`diagnostic`, `lifecycle`. Virtual timestamps are recorded at the simulated
source, not host receipt. `seq` orders equal timestamps. The 8192-event ring
drops oldest events and increments `Dropped` rather than blocking simulation.
Consumers must detect sequence gaps and resynchronize from paused `Snapshot`.
The snapshot includes physical/motor/sensor/fault state, run/tick, pending
count and drop count. Console messages remain optional, separate guest diagnostics.
No firmware log is an input to physical state.

## Security and delivery limits

The supported student launcher uses the same physical model through
[private runtime pipes and restricted loopback HTTP](browser.md).
Do not expose a developer monitor as a parking control interface.

**Headless does not mean listener-free.** Official Robot testing uses a
privileged control service, previously observed listening on wildcard TCP 49152;
the accepted test invocation still uses it. Run these developer-only finite
checks on an isolated/trusted host with inbound traffic blocked. They are not
the supported student workflow and do not demonstrate network containment.
Debugger support remains deferred. Use the published tooling revision selected
by the manifest; dependency/SVD locking and other-host qualification remain
release-review responsibilities.
