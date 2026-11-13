// SPDX-License-Identifier: Apache-2.0
// Course-owned STM32U5 ADC4 subset, not a replacement for the complete MCU.
// Register layout: STM32U5 HAL at 39130f29ae37c1db34095478ca02b6419b70dcdc.
using System;
using System.Collections.Generic;
using Antmicro.Renode.Core;
using Antmicro.Renode.Exceptions;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Peripherals.Bus;
using Antmicro.Renode.Peripherals.Timers;
using Antmicro.Renode.Time;

namespace Antmicro.Renode.Peripherals.Analog
{
    // Only three RCC words are mapped. Other fields in these shared words have
    // readback storage, not clock behavior. The inherited MCU boot clock
    // workarounds remain outside this ADC-specific extension.
    public class CourseU5ADCClock : IDoubleWordPeripheral
    {
        public CourseU5ADCClock(uint hclkFrequency = 160000000)
        {
            HclkFrequency = hclkFrequency;
        }

        public uint ReadDoubleWord(long offset)
        {
            switch(offset)
            {
            case 0x6c: return reset;
            case 0x94: return enable;
            case 0xe8: return selection;
            default: throw new RecoverableException("Unexpected ADC clock register");
            }
        }

        public void WriteDoubleWord(long offset, uint value)
        {
            switch(offset)
            {
            case 0x6c:
                reset = value;
                if(ResetAsserted) PeripheralReset?.Invoke();
                break;
            case 0x94: enable = value; break;
            case 0xe8: selection = value; break;
            default: throw new RecoverableException("Unexpected ADC clock register");
            }
        }

        public void Reset()
        {
            reset = enable = selection = 0;
        }

        public bool ResetAsserted => (reset & (1u << 5)) != 0;
        public bool Enabled => (enable & (1u << 5)) != 0 && !ResetAsserted;
        // ADCDACSEL=0 selects HCLK. Other sources deliberately are not modeled.
        public uint Frequency => Enabled && (selection & 0x7000) == 0 ? HclkFrequency : 0;
        public uint HclkFrequency { get; }
        public event Action PeripheralReset;
        private uint reset, enable, selection;
    }

    public class CourseSTM32U5ADC4 : IDoubleWordPeripheral, IKnownSize
    {
        public CourseSTM32U5ADC4(IMachine machine, CourseU5ADCClock clock)
        {
            this.clock = clock;
            IRQ = new GPIO();
            timer = new LimitTimer(machine.ClockSource, 1000000000, this, "ADC operation",
                direction: Direction.Ascending, workMode: WorkMode.OneShot, eventEnabled: true);
            timer.LimitReached += CompleteOperation;
            clock.PeripheralReset += Reset;
            Reset();
        }

        // Inputs are ideal 12-bit full-scale codes, independent of the selected
        // resolution. Lower resolutions discard low bits. No host-time actions.
        public void FeedSample(uint value, int channel)
        {
            if(value > 4095 || channel < 0 || channel > 23)
                throw new RecoverableException("Expected 12-bit sample and channel 0..23");
            if(sources.ContainsKey(channel))
                throw new RecoverableException("ADC4 channel has a connected physical sample source");
            if(!samples.ContainsKey(channel)) samples[channel] = new Queue<uint>();
            samples[channel].Enqueue(value);
        }

        public uint ReadDoubleWord(long offset)
        {
            switch((Registers)offset)
            {
            case Registers.Status: return status;
            case Registers.InterruptEnable: return interruptEnable;
            case Registers.Control: return control;
            case Registers.Configuration1: return configuration1;
            case Registers.Configuration2: return configuration2;
            case Registers.SamplingTime: return samplingTime;
            case Registers.ChannelSelection: return channels;
            case Registers.Data:
                status &= ~Eoc;
                UpdateInterrupt();
                return data;
            case Registers.CalibrationFactor: return 0; // Ideal offset calibration.
            case Registers.CommonConfiguration: return common;
            default:
                this.Log(LogLevel.Warning, "Unsupported ADC4 read at 0x{0:X}", offset);
                return 0;
            }
        }

        public void WriteDoubleWord(long offset, uint value)
        {
            if(!clock.Enabled) return; // Peripheral register bus is gated.
            switch((Registers)offset)
            {
            case Registers.Status:
                status &= ~(value & StatusMask);
                UpdateInterrupt();
                break;
            case Registers.InterruptEnable:
                interruptEnable = value & StatusMask;
                UpdateInterrupt();
                break;
            case Registers.Control: WriteControl(value); break;
            case Registers.Configuration1:
                if((control & Start) == 0) configuration1 = value;
                break;
            case Registers.Configuration2:
                if((control & Start) == 0) configuration2 = value;
                break;
            case Registers.SamplingTime:
                if((control & Start) == 0) samplingTime = value;
                break;
            case Registers.ChannelSelection:
                if((control & Start) == 0) channels = value & 0xffffff;
                break;
            case Registers.CommonConfiguration:
                if((control & Enable) == 0) common = value;
                break;
            default:
                this.Log(LogLevel.Warning, "Unsupported ADC4 write at 0x{0:X}: 0x{1:X}", offset, value);
                break;
            }
        }

        public void Reset()
        {
            timer.Reset();
            operation = Operation.None;
            status = interruptEnable = control = configuration1 = configuration2 = 0;
            samplingTime = channels = data = common = 0;
            regulatorReady = false;
            samples.Clear();
            CompletedConversions = 0;
            CompletedCalibrations = 0;
            IRQ.Unset();
        }

        public long Size => 0x400;
        public GPIO IRQ { get; }
        public uint CompletedConversions { get; private set; }
        public uint CompletedCalibrations { get; private set; }
        // Optional continuously connected per-channel source, sampled at
        // conversion completion. Connection survives reset, just as GPIO wiring
        // does; source state resets with its owning peripheral. A channel has at
        // most one source, and a connected channel rejects FIFO injection.
        public void ConnectSource(int channel, Func<uint> source)
        {
            if(channel < 0 || channel > 23 || source == null)
                throw new RecoverableException("Expected channel 0..23 and a source");
            if(sources.ContainsKey(channel))
                throw new RecoverableException("ADC4 channel already has a connected source");
            if(samples.TryGetValue(channel, out var pending) && pending.Count != 0)
                throw new RecoverableException("ADC4 channel has injected FIFO samples");
            sources[channel] = source;
        }
        public int PendingSamples
        {
            get
            {
                var count = 0;
                foreach(var queue in samples.Values) count += queue.Count;
                return count;
            }
        }

        private void WriteControl(uint value)
        {
            // ADVREGEN is RW; command bits ADEN/ADDIS/ADSTART/ADSTP/ADCAL are
            // handled as requests, not ordinary writable storage.
            if((value & Regulator) == 0)
            {
                timer.Enabled = false;
                operation = Operation.None;
                control = 0;
                status &= ~LdoReady;
                regulatorReady = false;
            }
            else if((control & Regulator) == 0)
            {
                control |= Regulator;
                Begin(Operation.Regulator, 20000); // 20 us regulator abstraction.
            }
            if((value & Stop) != 0)
            {
                if(operation == Operation.Conversion) timer.Enabled = false;
                operation = Operation.None;
                control &= ~Start;
            }
            if((value & Disable) != 0 && (control & Start) == 0)
                control &= ~Enable;
            if((value & Calibrate) != 0 && (control & Enable) == 0 &&
                regulatorReady && AdcFrequency != 0)
            {
                control |= Calibrate;
                Begin(Operation.Calibration, CyclesToNanoseconds(100));
            }
            if((value & Enable) != 0 && (control & Enable) == 0 &&
                regulatorReady && AdcFrequency != 0 && operation == Operation.None)
            {
                control |= Enable;
                Begin(Operation.Enable, CyclesToNanoseconds(2));
            }
            if((value & Start) != 0 && (control & Enable) != 0 &&
                operation == Operation.None)
                StartConversion();
            UpdateInterrupt();
        }

        private void StartConversion()
        {
            // Fail visibly rather than falsely completing an unsupported mode.
            // Single selected channel, fixed sequencer, software trigger,
            // no DMA/continuous/discontinuous/auto-off/watchdog/oversampling.
            if((configuration1 & ~0x102cu) != 0 || configuration2 != 0 ||
                (common & ~0x3c0000u) != 0 || channels == 0 ||
                (channels & (channels - 1)) != 0 || AdcFrequency == 0)
            {
                this.Log(LogLevel.Error, "Unsupported ADC4 configuration or clock; conversion rejected");
                return;
            }
            selectedChannel = 0;
            while((channels & (1u << selectedChannel)) == 0) selectedChannel++;
            control |= Start;
            var samplingIndex = (samplingTime & (1u << (8 + selectedChannel))) == 0
                ? (samplingTime & 7) : ((samplingTime >> 4) & 7);
            var resolution = 12 - 2 * (int)((configuration1 >> 2) & 3);
            // Half-cycle sampling times + (resolution + 0.5) processing cycles.
            var halfCycles = SampleHalfCycles[samplingIndex] + (uint)(2 * resolution + 1);
            Begin(Operation.Conversion, CyclesToNanoseconds(halfCycles, 2));
        }

        private uint AdcFrequency
        {
            get
            {
                var prescaler = (common >> 18) & 15;
                if((common & (3u << 16)) != 0 || prescaler >= Divisors.Length) return 0;
                var frequency = clock.Frequency / Divisors[prescaler];
                return frequency <= 55000000 ? frequency : 0;
            }
        }

        private ulong CyclesToNanoseconds(uint cycles, uint denominator = 1)
        {
            var frequency = (ulong)AdcFrequency * denominator;
            return frequency == 0 ? 1 : (cycles * 1000000000UL + frequency - 1) / frequency;
        }

        private void Begin(Operation next, ulong nanoseconds)
        {
            timer.Enabled = false;
            operation = next;
            timer.Limit = nanoseconds;
            timer.Value = 0;
            timer.Enabled = true;
        }

        private void CompleteOperation()
        {
            var completed = operation;
            operation = Operation.None;
            // Clock switching/gating during an operation is outside this subset.
            // It must never generate a conversion or a ready indication.
            if(clock.Frequency == 0) return;
            switch(completed)
            {
            case Operation.Regulator:
                regulatorReady = true;
                status |= LdoReady;
                break;
            case Operation.Calibration:
                control &= ~Calibrate;
                CompletedCalibrations++;
                status |= EndCalibration;
                break;
            case Operation.Enable: status |= Ready; break;
            case Operation.Conversion:
                uint sample = 0;
                if(sources.TryGetValue(selectedChannel, out var source))
                    sample = source();
                else if(samples.TryGetValue(selectedChannel, out var queue) && queue.Count != 0)
                    sample = queue.Dequeue();
                else
                    this.Log(LogLevel.Warning, "ADC4 channel {0} has no injected sample; using ground", selectedChannel);
                if((status & Eoc) != 0) status |= Overrun;
                // OVRMOD=0 preserves unread data; OVRMOD=1 overwrites it.
                if((status & Eoc) == 0 || (configuration1 & (1u << 12)) != 0)
                {
                    var shift = 2 * (int)((configuration1 >> 2) & 3);
                    data = sample >> shift;
                    if((configuration1 & (1u << 5)) != 0) data <<= 4 + shift;
                }
                status |= EndSampling | Eoc | EndSequence;
                control &= ~Start;
                CompletedConversions++;
                break;
            }
            UpdateInterrupt();
        }

        private void UpdateInterrupt() => IRQ.Set((status & interruptEnable & StatusMask) != 0);

        private readonly CourseU5ADCClock clock;
        private readonly LimitTimer timer;
        private readonly Dictionary<int, Queue<uint>> samples = new Dictionary<int, Queue<uint>>();
        private readonly Dictionary<int, Func<uint>> sources = new Dictionary<int, Func<uint>>();
        private uint status, interruptEnable, control, configuration1, configuration2;
        private uint samplingTime, channels, data, common;
        private int selectedChannel;
        private bool regulatorReady;
        private Operation operation;
        private static readonly uint[] Divisors = { 1, 2, 4, 6, 8, 10, 12, 16, 32, 64, 128, 256 };
        private static readonly uint[] SampleHalfCycles = { 3, 7, 15, 25, 39, 79, 159, 1629 };
        private const uint Ready = 1, EndSampling = 2, Eoc = 4, EndSequence = 8, Overrun = 16;
        private const uint EndCalibration = 1u << 11, LdoReady = 1u << 12;
        private const uint StatusMask = 0x181f;
        private const uint Enable = 1, Disable = 2, Start = 4, Stop = 16;
        private const uint Regulator = 1u << 28, Calibrate = 1u << 31;
        private enum Operation { None, Regulator, Calibration, Enable, Conversion }
        private enum Registers
        {
            Status = 0, InterruptEnable = 4, Control = 8, Configuration1 = 0xc,
            Configuration2 = 0x10, SamplingTime = 0x14, ChannelSelection = 0x28,
            Data = 0x40, CalibrationFactor = 0xc4, CommonConfiguration = 0x308
        }
    }
}
