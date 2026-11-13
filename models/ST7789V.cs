// SPDX-License-Identifier: Apache-2.0
// ST7789V four-wire SPI command subset. Pixels come only from received SPI bytes.
using System;
using System.Collections.Generic;
using System.Text;
using Antmicro.Renode.Core;
using Antmicro.Renode.Exceptions;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Peripherals.SPI;

namespace Antmicro.Renode.Peripherals.Video
{
    public class CourseST7789V : ISPIPeripheral, IGPIOReceiver
    {
        public CourseST7789V() { Reset(); }

        // GPIO 0: active-low CS; 1: D/C (high=data); 2: active-low reset.
        public void OnGPIO(int number, bool value)
        {
            switch(number)
            {
            case 0:
                if(cs && !value) Selections++;
                if(!cs && value && selectedHasData) Transactions++;
                if(value) selectedHasData = false;
                cs = value;
                break;
            case 1: dc = value; break;
            case 2:
                if(resetPin && !value)
                {
                    ClearController();
                    HardwareResets++;
                    resetSeen = true;
                }
                resetPin = value;
                break;
            default: throw new RecoverableException("Display GPIO must be CS=0, DC=1 or reset=2");
            }
        }

        public byte Transmit(byte value)
        {
            Transfers++;
            if(cs || !resetPin || !resetSeen)
            {
                Reject("SPI byte while deselected, held in reset, or before hardware reset");
                return 0;
            }
            selectedHasData = true;
            if(!dc) Command(value);
            else Data(value);
            return 0;
        }

        // Controller transaction end is not physical CS: the MCU SPI driver
        // may split command and parameters into separately selected transfers.
        public void FinishTransmission() { }

        public void Reset()
        {
            // GPIO outputs start low. Remain held in reset until the guest
            // configures the pin, and require its subsequent reset pulse.
            cs = true;
            resetPin = dc = resetSeen = selectedHasData = false;
            Transfers = Transactions = Selections = HardwareResets = PixelWrites = Errors = 0;
            DroppedCommands = 0;
            commands.Clear();
            // Deterministic machine power-cycle state, not an assertion that
            // real ST7789V frame RAM powers on as zero.
            Array.Clear(pixels, 0, pixels.Length);
            ClearController();
        }

        public uint GetPixel(int x, int y)
        {
            if(x < 0 || x >= Width || y < 0 || y >= Height)
                throw new RecoverableException("Pixel outside 240x320 display");
            return pixels[y * Width + x];
        }
        public string Commands => string.Join(" ", commands);
        // Paused bridge export: fixed native row-major RGB565, four hex
        // characters/pixel, independent of console output or a browser.
        public string PixelState
        {
            get
            {
                var text = new StringBuilder(Width * Height * 4);
                foreach(var pixel in pixels) text.Append(pixel.ToString("X4"));
                return text.ToString();
            }
        }
        public string Snapshot => "{\"v\":1,\"width\":240,\"height\":320," +
            "\"format\":\"RGB565\",\"initialized\":" + (Initialized ? "true" : "false") +
            ",\"pixel_writes\":" + PixelWrites + ",\"errors\":" + Errors +
            ",\"dropped_commands\":" + DroppedCommands + "}";
        public bool Initialized => awake && displayOn && format == 0x55 && orientation == 0;
        public uint Transfers { get; private set; }
        public uint Transactions { get; private set; }
        public uint Selections { get; private set; }
        public uint HardwareResets { get; private set; }
        public uint PixelWrites { get; private set; }
        public uint Errors { get; private set; }
        public uint DroppedCommands { get; private set; }

        private void ClearController()
        {
            awake = displayOn = false;
            format = 0x66;
            orientation = 0;
            command = 0;
            needed = received = 0;
            x0 = y0 = x = y = 0;
            x1 = Width - 1;
            y1 = Height - 1;
            highByte = -1;
        }

        private void Command(byte value)
        {
            if(received != needed || highByte >= 0) Reject("Incomplete command parameters or pixel");
            command = value;
            needed = received = 0;
            highByte = -1;
            if(commands.Count == 128) { commands.RemoveAt(0); DroppedCommands++; }
            commands.Add(value.ToString("X2"));
            switch(value)
            {
            case 0x01:
                // SWRESET retains frame RAM, COLMOD, MADCTL and address
                // windows (ST7789V v1.3 command default tables).
                awake = displayOn = false;
                command = 0;
                x = x0; y = y0;
                break;
            case 0x11: awake = true; break;       // SLPOUT
            case 0x28: displayOn = false; break;
            case 0x29: displayOn = true; break;
            case 0x3a: case 0x36: needed = 1; break; // COLMOD / MADCTL
            case 0x2a: case 0x2b: needed = 4; break; // CASET / RASET
            case 0x2c:
                if(!Initialized) Reject("RAMWR before supported initialization");
                x = x0; y = y0;
                break;
            default:
                command = 0;
                Reject("Unsupported ST7789V command");
                break;
            }
        }

        private void Data(byte value)
        {
            if(command == 0x2c)
            {
                if(!Initialized || x0 > x1 || y0 > y1 || x1 >= Width || y1 >= Height)
                {
                    Reject("Unsupported display mode or invalid address window");
                    return;
                }
                if(highByte < 0) { highByte = value; return; }
                pixels[y * Width + x] = (ushort)((highByte << 8) | value);
                highByte = -1;
                PixelWrites++;
                if(++x > x1) { x = x0; if(++y > y1) y = y0; }
                return;
            }
            if(received >= needed)
            {
                Reject("Unexpected parameter byte");
                return;
            }
            parameters[received++] = value;
            if(received != needed) return;
            switch(command)
            {
            case 0x3a:
                format = value;
                if(format != 0x55) Reject("Only RGB565 is supported");
                break;
            case 0x36:
                orientation = value;
                if(orientation != 0) Reject("Only native RGB orientation is supported");
                break;
            case 0x2a:
                x0 = (parameters[0] << 8) | parameters[1];
                x1 = (parameters[2] << 8) | parameters[3];
                break;
            case 0x2b:
                y0 = (parameters[0] << 8) | parameters[1];
                y1 = (parameters[2] << 8) | parameters[3];
                break;
            }
        }

        private void Reject(string message)
        {
            Errors++;
            this.Log(LogLevel.Warning, message);
        }

        private const int Width = 240, Height = 320;
        private readonly ushort[] pixels = new ushort[Width * Height];
        private readonly byte[] parameters = new byte[4];
        private readonly List<string> commands = new List<string>();
        private bool cs, dc, resetPin, resetSeen, selectedHasData, awake, displayOn;
        private byte command, format, orientation;
        private int needed, received, x0, x1, y0, y1, x, y, highByte;
    }
}
