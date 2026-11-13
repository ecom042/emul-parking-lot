# Hardware models and supported interfaces

## Board and loading

The laboratory retains `nucleo_u5a5zj_q` and standard Zephyr GPIO, ADC, UART and
SPI APIs. Shared configuration is in `hardware/parking.overlay` and
`hardware/renode/parking.repl`. Students implement driver interaction; solved
peripheral probes are not part of this distribution.

Generated MCU descriptions are a starting point, not proof of peripheral
compatibility. Additive REPL `preinit` sections load local C# sources through
Renode's `include` mechanism and replace incompatible generated devices before
registration. This does not require installing a modified Renode or changing the
Zephyr driver/HAL.

The `Course` class prefix identifies custom laboratory subsets, not upstream
Renode models or manufacturer implementations.

## Pin map

The lot has two independent lanes. Both use the same equipment contract; only
the exit lane owns the payment terminal and its display. Devicetree properties
in `zephyr,user` carry an `entry-` or `exit-` prefix (for example
`entry-enable-gpios`, `exit-red-gpios`).

| Interface | Entry lane | Exit lane |
|---|---|---|
| Motor | PB12 enable, PB13 direction (high opens) | PB1 enable, PB2 direction (high opens) |
| Limits (active high, pull down) | PC5 upper / PC6 lower; EXTI IRQ 16 / 17 | PC2 upper / PC3 lower; EXTI IRQ 13 / 14 |
| Passage/obstruction beam | PC8 active high; EXTI IRQ 19 | PC4 active high; EXTI IRQ 15 |
| Red / green / buzzer | PE2 / PE3 / PE4 active high | PD1 / PD2 / PD3 active high |
| Distance | ADC4 channel 17 / PD13 | ADC4 channel 18 / PB0 |
| Camera | USART3 PD8 TX / PD9 RX, IRQ 63 | USART2 PD5 TX / PD6 RX, IRQ 62 |
| Payment | none | LPUART1 PG7 TX / PG8 RX, IRQ 66 |
| Display | none | SPI1 PA5 SCK / PA6 MISO / PA7 MOSI; PD14 active-low CS, PD15 D/C, PE0 active-low reset |

ADC4 (address `0x46021000`, IRQ 113) is shared by both lanes; the console stays
on USART1 PA9 TX / PA10 RX, IRQ 61. ADC4 IN19 is not usable for distance
because it shares PB1 with the exit motor enable.

Firmware controls outputs. Equipment models provide sensor inputs and device
replies. A model diagnostic or endpoint clamp must not disable the guest motor.

## ADC4

`models/STM32U5_ADC4.cs` implements the register layout used by the pinned U5 HAL.
ADC1 is disabled in the adopted configuration and is not supported.

| Guest operation | Model rule |
|---|---|
| Bus enable | RCC AHB3ENR `0x46020c94`, bit 5; gated bus ignores ADC writes |
| Kernel clock | RCC CCIPR3 `0x46020ce8`, ADCDACSEL bits 14:12; selection 0 is fixed HCLK |
| Prescaler | ADC4 CCR `0x46021308`, PRESC bits 21:18; board /4 gives 40 MHz from declared 160 MHz HCLK; reject >55 MHz |
| Regulator | CR `+0x08`, ADVREGEN bit 28; 20 us virtual startup sets ISR LDORDY bit 12 |
| Calibration | ADCAL bit 31 clears on scheduled completion; ideal factor 0, EOCAL bit 11; consumes no input |
| Enable/stop | ADEN/ADDIS requests, scheduled ADRDY; ADSTP cancels conversion |
| Channel | CHSELR `+0x28`, exactly one bit selecting channel 0..23 (alternate IN17/IN18 between conversions) |
| Acquisition | SMPR `+0x14`, two common sample-time fields and channel selectors |
| Resolution | CFGR1 `+0x0c`, RES bits 3:2; 12/10/8/6-bit, ALIGN bit 5 |
| Conversion | ADSTART bit 2 clears on completion; requires regulator, enable and supported clock/configuration |
| Acknowledgment | DR `+0x40` read clears EOC; implemented ISR flags are write-one-to-clear |
| IRQ | Enabled pending status drives a level interrupt at NVIC 113 |
| Overrun | Unread EOC sets OVR; OVRMOD=0 preserves data, OVRMOD=1 overwrites |
| Reset | Machine reset or RCC AHB3RSTR `0x46020c6c` bit 5 clears state, timer, IRQ, queues and counters |

The ADC owns a `0x400`-byte aperture including CCR. The clock adapter maps only
the three RCC words above. Non-ADC fields in them have readback storage, not
functional clock/reset behavior. Inherited oscillator/PLL/PWR/ICACHE workarounds
remain separate. This is **not a complete clock-tree model**.

### Injection and scaling

`FeedSample(code, channel)` accepts ideal unsigned 12-bit full-scale codes
0..4095 on channels 0..23. One queued code is consumed for the selected channel
at conversion completion. Other-channel queues remain untouched. Empty input
produces ground with a warning; reset clears queues.

Lower resolutions discard low code bits. Use gain 1 and Zephyr's
internal-reference metadata of 3300 mV; `adc_raw_to_millivolts` calculates
integer `raw * 3300 / 4096`. Thus 0 → 0 mV, 2048 → 1650 mV and 4095 → 3299 mV.
The [physical model](physical-model.md) supplies its own distance-to-code curve.

Each lane's physical model connects a continuous per-channel source with
`ConnectSource(channel, source)`: the entry lane on channel 17, the exit lane
on channel 18. A channel accepts one source, and a connected channel rejects
`FeedSample`; other channels keep FIFO injection. Connections survive reset
like GPIO wiring, while the owning physical state resets.

The Zephyr ADC context lock is held for a whole read, even an asynchronous one.
Firmware should own ADC4 from one place and alternate the two single-channel
conversions, rather than issuing concurrent reads from two lane threads.

A 1 GHz virtual timer schedules regulator startup, 100-cycle ideal calibration,
two-cycle enable and conversion after sampling plus resolution +0.5 cycles,
rounded up to nanoseconds. These are functional abstractions, not analog
settling or cycle-accurate silicon timing.

### Unsupported ADC behavior

ADC1/ADC2, multi-channel sequences, external triggers, DMA, continuous/
discontinuous/auto-off modes, oversampling, analog watchdogs, differential
inputs, internal sensor physics, calibration accuracy, dynamic clock switching,
low-power/resume, security aliases and electrical supply accuracy are not
supported. Only 32-bit MMIO is implemented. Unsupported conversion modes log an
error without producing a conversion; unknown register accesses warn.

## GPIO and EXTI

`models/STM32U5_EXTI.cs` models GPIO lines 0..15 with port A..I receivers,
8-bit source selections in EXTICR1..4, edge selection, separate rising/falling
pending banks, masking and write-one-to-clear acknowledgment. Only the selected
port can raise a line. Repeated equal levels are not edges; unmasking an already
pending line asserts its IRQ.

Shared wiring routes port C into EXTI and lines 0..15 to NVIC IRQs 11..26.
Both lanes' limit and beam inputs are on port C (lines 2..6 and 8), so each has
its own EXTI line and IRQ; line 13 belongs to the board button.
Firmware owns the response to a limit interrupt; the model does not clear motor
enable. Direct receiver tests need a matching-port positive control:
stimulating an unconnected GPIO port does not test EXTI port rejection.

Internal sources, event/wakeup mode, low-power behavior, secure/privileged
registers and electrical bounce are outside this subset.

## UART and SPI controller limitations

Generated `STM32F7_USART` instances provide independent console, entry camera,
exit camera and payment
byte streams. The supported behavior is fragmented bidirectional traffic and log
isolation, not bit-level baud timing, DMA or general application IRQ transport.
Generated UART frequencies can differ from the firmware's declared clock
(200 MHz versus 160 MHz); do not infer baud accuracy from byte delivery.

SPI uses synchronous `STM32H7_SPI`. Functional transfers do not validate SPI
waveforms, clock timing or every pin-mux/electrical property.

## Display

`models/ST7789V.cs` consumes actual SPI bytes and GPIO CS/D/C/reset. It implements
native 240×320 RGB565, high byte first, with commands `01`, `11`, `28`, `29`,
`3A`, `36`, `2A`, `2B`, `2C`. COLMOD must be `55`, MADCTL `00`; address windows
must be valid. This is not a claim that the complete Zephyr display driver works.

Guest startup asserts reset for 10 ms, waits 120 ms after release, 150 ms after
SWRESET and 120 ms after SLPOUT. The model does not enforce those delays.
See the [ST7789V v1.3 datasheet](https://newhavendisplay.com/content/datasheets/ST7789V.pdf)
for manufacturer behavior; the PDF is not bundled.

The model rejects data outside selection/reset, missing reset, unexpected or
incomplete parameters, unsupported modes and RAMWR before initialization.
Physical GPIO CS delimits transactions; `FinishTransmission` alone is not CS.
Commands and parameters may span selected transfers.

Hardware reset clears controller state but retains frame RAM. SWRESET also
retains the subset's format/orientation/address fields. Machine reset clears
RAM/counters deterministically; that is a laboratory power-cycle abstraction.
Readback, other formats/orientations, inversion, gamma, scrolling, TE and panel
electrical physics are unsupported.

Paused `PixelState` exports 307200 hex characters, four per RGB565 pixel.
Snapshots expose dimensions, initialization/error/write counters and bounded
command history. Pixels are never inferred from console messages.
