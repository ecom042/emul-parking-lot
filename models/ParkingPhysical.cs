// SPDX-License-Identifier: Apache-2.0
using System;
using System.Collections.Generic;
using Antmicro.Renode.Core;
using Antmicro.Renode.Exceptions;
using Antmicro.Renode.Extensions.Equipment;
using Antmicro.Renode.Peripherals.Analog;

namespace Antmicro.Renode.Peripherals.Miscellaneous
{
    // Course engineering model. GPIO commands are observed, never overwritten.
    public sealed class ParkingPhysical : IPeripheral, IGPIOReceiver, INumberedGPIOOutput
    {
        // One instance per lane. The lane is a validated label for snapshots;
        // event device names stay lane-independent so oracles apply per owner.
        public ParkingPhysical(IMachine machine, CourseSTM32U5ADC4 adc,
            int adcChannel = 18, string lane = "exit")
        {
            if(lane != "entry" && lane != "exit")
                throw new RecoverableException("Lane must be entry or exit");
            this.machine = machine;
            Lane = lane;
            Connections = new Dictionary<int, IGPIO> {
                { 0, new GPIO() }, { 1, new GPIO() }, { 2, new GPIO() }
            };
            timeline = new EquipmentTimeline(machine, Step, Validate, Execute);
            adc.ConnectSource(adcChannel, () => (uint)AdcCode);
            Reset();
        }

        public void Reset()
        {
            timeline.Reset();
            enabled = direction = jam = obstruction = false;
            previousDirection = false;
            driven = deadlineReported = false;
            reverseTicks = motionTicks = 0;
            Position = Speed = 0;
            Vehicle = 6000;
            vehicleSpeed = 0;
            random = 1;
            noise = 0;
            limitFault = 0;
            AdcCode = 4095;
            PublishSensors();
            timeline.Emit("physical", "barrier", "position_mdeg", Position);
        }

        // Initial scene configuration only, never a running position setter.
        // Reset restores the normal defaults, not this test's initial scene.
        public void ConfigureInitial(int positionMillidegrees, int vehicleMillimetres, int seed)
        {
            if(!machine.IsPaused || Tick != 0 || enabled ||
                positionMillidegrees < 0 || positionMillidegrees > 90000 ||
                vehicleMillimetres < -2000 || vehicleMillimetres > 6000 || seed <= 0)
                throw new RecoverableException("Initial scene requires paused tick zero and valid position/vehicle/seed");
            Position = positionMillidegrees;
            Vehicle = vehicleMillimetres;
            random = (uint)seed;
            AdcCode = Math.Min(6000, Math.Abs(Vehicle)) * 4095 / 6000;
            PublishSensors();
            timeline.Emit("physical", "barrier", "position_mdeg", Position);
        }

        public void OnGPIO(int number, bool value)
        {
            if(number != 0 && number != 1)
                throw new RecoverableException("Motor inputs are enable=0 and direction=1");
            if(number == 0)
            {
                if(enabled == value) return;
                enabled = value;
                if(!enabled) { motionTicks = 0; deadlineReported = false; }
            }
            else
            {
                if(direction == value) return;
                direction = value;
            }
            timeline.Emit("commanded", "motor", number == 0 ? "enable" : "direction", value ? 1 : 0);
        }

        public string Submit(int version, int run, int id, ulong atTick, string signal, int value)
            => timeline.Submit(version, run, id, atTick, signal, value);
        public string Trace => timeline.Trace;
        public string TraceAfter(ulong sequence) => timeline.TraceAfter(sequence);
        public int Run => timeline.Run;
        public ulong Tick => timeline.Tick;
        public int Pending => timeline.Pending;
        public ulong Dropped => timeline.Dropped;
        public bool Enabled => enabled;
        public bool Direction => direction;
        public int Position { get; private set; }
        public int Speed { get; private set; }
        public int Vehicle { get; private set; }
        public int AdcCode { get; private set; }
        public string Lane { get; }
        public string Snapshot => "{\"v\":1,\"lane\":\"" + Lane + "\",\"run\":" + Run + ",\"tick\":" + Tick +
            ",\"seq\":" + timeline.Sequence + ",\"time_us\":" + timeline.TimeMicroseconds +
            ",\"position_mdeg\":" + Position + ",\"speed_mdeg_s\":" + Speed +
            ",\"vehicle_mm\":" + Vehicle + ",\"adc_code\":" + AdcCode +
            ",\"enable\":" + (enabled ? "true" : "false") +
            ",\"direction\":" + (direction ? "true" : "false") +
            ",\"obstruction\":" + (obstruction ? "true" : "false") +
            ",\"jam\":" + (jam ? "true" : "false") +
            ",\"limit_fault\":" + limitFault +
            ",\"pending\":" + Pending + ",\"dropped\":" + Dropped + "}";
        public IReadOnlyDictionary<int, IGPIO> Connections { get; }

        private bool Validate(string signal, int value)
        {
            switch(signal)
            {
            case "fault.jam":
            case "sensor.obstruction": return value == 0 || value == 1;
            case "fault.limits": return value >= 0 && value <= 3;
            case "vehicle.next": return value == 1;
            case "vehicle.speed": return value >= -2000 && value <= 2000 && value % 100 == 0;
            case "sensor.noise": return value >= 0 && value <= 64;
            case "sensor.seed": return value > 0;
            default: return false;
            }
        }

        private void Execute(string signal, int value)
        {
            switch(signal)
            {
            case "fault.jam": jam = value != 0; break;
            case "fault.limits": limitFault = value; break;
            case "sensor.obstruction": obstruction = value != 0; break;
            case "vehicle.next":
                if(Vehicle != -2000 || vehicleSpeed != 0 || Position != 0 || enabled)
                    throw new RecoverableException("Next vehicle requires completed passage and closed stopped barrier");
                Vehicle = 6000;
                break;
            case "vehicle.speed": vehicleSpeed = value; break;
            case "sensor.noise": noise = value; break;
            case "sensor.seed": random = (uint)value; break;
            }
        }

        private void Step()
        {
            // Latched GPIOs; fixed 10 ms integration, 45 deg/s, no inertia.
            if(driven && direction != previousDirection) reverseTicks = 10;
            previousDirection = direction;
            if(enabled) driven = true;
            Speed = enabled && !jam && reverseTicks == 0 ? (direction ? 45000 : -45000) : 0;
            if(reverseTicks > 0) reverseTicks--;
            var oldPosition = Position;
            Position = Math.Max(0, Math.Min(90000, Position + Speed / 100));
            if(Position == oldPosition) Speed = 0;
            timeline.Emit("physical", "barrier", "position_mdeg", Position);
            timeline.Emit("physical", "barrier", "speed_mdeg_s", Speed);
            if(enabled && ++motionTicks > 250 && !deadlineReported)
            {
                timeline.Emit("diagnostic", "motor", "enabled_deadline", 1);
                deadlineReported = true;
            }
            if(enabled && (jam || (direction ? Position == 90000 : Position == 0)))
                timeline.Emit("diagnostic", "motor", jam ? "jam_enabled" : "endpoint_enabled", 1);

            Vehicle = Math.Max(-2000, Math.Min(6000, Vehicle + vehicleSpeed / 100));
            timeline.Emit("physical", "vehicle", "position_mm", Vehicle);
            // Sensor holds a value across guest conversions; one noise draw/tick.
            random = unchecked(random * 1664525u + 1013904223u);
            int perturbation = noise == 0 ? 0 : (int)(random % (uint)(2 * noise + 1)) - noise;
            var distance = Math.Min(6000, Math.Abs(Vehicle));
            AdcCode = Math.Max(0, Math.Min(4095, distance * 4095 / 6000 + perturbation));
            PublishSensors();
        }

        private void PublishSensors()
        {
            var upper = limitFault == 3 || (limitFault != 1 && Position == 90000);
            var lower = limitFault == 3 || (limitFault != 2 && Position == 0);
            Drive(0, upper);
            Drive(1, lower);
            Drive(2, obstruction);
            timeline.Emit("sensor", "limits", "upper", upper ? 1 : 0);
            timeline.Emit("sensor", "limits", "lower", lower ? 1 : 0);
            timeline.Emit("sensor", "beam", "obstruction", obstruction ? 1 : 0);
            timeline.Emit("sensor", "distance", "adc_code", AdcCode);
        }

        private void Drive(int index, bool value)
        {
            var output = Connections[index];
            // A receiver can reset after this source. Re-drive held levels at
            // each sensor sample without manufacturing a low/high source edge.
            if(output.IsSet == value)
            {
                foreach(var endpoint in output.Endpoints)
                    endpoint.Receiver.OnGPIO(endpoint.Number, value);
            }
            else output.Set(value);
        }

        private readonly IMachine machine;
        private readonly EquipmentTimeline timeline;
        private bool enabled, direction, previousDirection, driven, jam, obstruction, deadlineReported;
        private int reverseTicks, motionTicks, vehicleSpeed, noise, limitFault;
        private uint random;
    }
}
