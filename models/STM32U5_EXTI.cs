// SPDX-License-Identifier: Apache-2.0
// Course-owned GPIO EXTI subset. U5 register layout, not a complete EXTI model.
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using Antmicro.Renode.Core;
using Antmicro.Renode.Exceptions;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Peripherals.Bus;

namespace Antmicro.Renode.Peripherals.IRQControllers
{
    public class CourseSTM32U5EXTI : IDoubleWordPeripheral, IKnownSize,
        ILocalGPIOReceiver, INumberedGPIOOutput
    {
        public CourseSTM32U5EXTI()
        {
            var outputs = new Dictionary<int, IGPIO>();
            for(var i = 0; i < 16; ++i) outputs[i] = new GPIO();
            Connections = new ReadOnlyDictionary<int, IGPIO>(outputs);
            for(var port = 0; port < 9; ++port) receivers[port] = new Receiver(this, port);
            Reset();
        }

        public IGPIOReceiver GetLocalReceiver(int index)
        {
            if(index < 0 || index >= receivers.Length)
                throw new RecoverableException("EXTI GPIO port must be A..I (0..8)");
            return receivers[index];
        }

        public uint ReadDoubleWord(long offset)
        {
            if(offset >= 0x60 && offset <= 0x6c && offset % 4 == 0)
                return mappings[(offset - 0x60) / 4];
            switch(offset)
            {
            case 0: return rising;
            case 4: return falling;
            case 8: return 0;
            case 0xc: return risingPending;
            case 0x10: return fallingPending;
            case 0x80: return mask;
            default:
                this.Log(LogLevel.Warning, "Unsupported EXTI read at 0x{0:X}", offset);
                return 0;
            }
        }

        public void WriteDoubleWord(long offset, uint value)
        {
            if(offset >= 0x60 && offset <= 0x6c && offset % 4 == 0)
            {
                mappings[(offset - 0x60) / 4] = value;
                return;
            }
            switch(offset)
            {
            case 0: rising = value & 0xffff; break;
            case 4: falling = value & 0xffff; break;
            case 8: risingPending |= value & 0xffff; break;
            case 0xc: risingPending &= ~value; break;
            case 0x10: fallingPending &= ~value; break;
            case 0x80: mask = value & 0xffff; break;
            default:
                this.Log(LogLevel.Warning, "Unsupported EXTI write at 0x{0:X}: 0x{1:X}", offset, value);
                return;
            }
            Update();
        }

        public void Reset()
        {
            rising = falling = risingPending = fallingPending = mask = 0;
            Array.Clear(mappings, 0, mappings.Length);
            foreach(var receiver in receivers) receiver.Clear();
            RisingEdges = FallingEdges = 0;
            Update();
        }

        public long Size => 0x400;
        public IReadOnlyDictionary<int, IGPIO> Connections { get; }
        public uint RisingEdges { get; private set; }
        public uint FallingEdges { get; private set; }

        private void Edge(int port, int pin, bool value)
        {
            var source = (mappings[pin / 4] >> (8 * (pin % 4))) & 0xff;
            if(source != port) return;
            uint bit = 1u << pin;
            if(value && (rising & bit) != 0)
            {
                risingPending |= bit;
                RisingEdges++;
            }
            if(!value && (falling & bit) != 0)
            {
                fallingPending |= bit;
                FallingEdges++;
            }
            Update();
        }

        private void Update()
        {
            for(var pin = 0; pin < 16; ++pin)
                Connections[pin].Set(((risingPending | fallingPending) & mask & (1u << pin)) != 0);
        }

        private class Receiver : IGPIOReceiver
        {
            public Receiver(CourseSTM32U5EXTI parent, int port)
            {
                this.parent = parent;
                this.port = port;
            }
            public void OnGPIO(int number, bool value)
            {
                if(number < 0 || number >= state.Length)
                    throw new RecoverableException("EXTI GPIO pin must be 0..15");
                if(state[number] == value) return;
                state[number] = value;
                parent.Edge(port, number, value);
            }
            public void Reset() { Clear(); }
            public void Clear() { Array.Clear(state, 0, state.Length); }
            private readonly bool[] state = new bool[16];
            private readonly CourseSTM32U5EXTI parent;
            private readonly int port;
        }

        private readonly Receiver[] receivers = new Receiver[9];
        private readonly uint[] mappings = new uint[4];
        private uint rising, falling, risingPending, fallingPending, mask;
    }
}
