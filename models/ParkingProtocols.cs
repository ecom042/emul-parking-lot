// SPDX-License-Identifier: Apache-2.0
using System;
using System.Collections.Generic;
using System.Text;
using System.Text.RegularExpressions;
using Antmicro.Renode.Core;
using Antmicro.Renode.Exceptions;
using Antmicro.Renode.Extensions.Equipment;
using Antmicro.Renode.Peripherals.UART;

namespace Antmicro.Renode.Peripherals.Miscellaneous
{
    // Course ASCII/LF v1 devices, connected to the actual MCU UARTs.
    public sealed class ParkingProtocols : IPeripheral, IGPIOReceiver
    {
        public ParkingProtocols(IMachine machine, IUART camera, IUART payment)
        {
            this.machine = machine;
            this.camera = camera;
            this.payment = payment;
            timeline = new EquipmentTimeline(machine, Step, Validate, Execute);
            camera.CharReceived += b => Receive(camInput, b, true);
            payment.CharReceived += b => Receive(payInput, b, false);
            Reset();
        }

        public void Reset()
        {
            timeline.Reset();
            camInput.Clear(); payInput.Clear();
            camDiscard = payDiscard = false;
            camTimeout = payTimeout = false;
            camStarted = payStarted = 0;
            replies.Clear(); captures.Clear(); charges.Clear();
            confidence = new[] { 93 };
            captureIndex = 0;
            plate = "ABC1D23";
            delay = payDelay = 10; fragment = payFragment = 8;
            cameraMissing = paymentMissing = false;
            paymentResult = "PAID";
            lastPayment = lastUuid = "";
            Requests = Captures = Payments = Errors = Replies = 0;
            red = green = buzzer = false;
        }

        // Trusted, paused configuration, not an arbitrary frame injection API.
        public void ConfigureCamera(string newPlate, string confidences, int delayTicks,
            int fragmentBytes, bool missing)
        {
            if(!machine.IsPaused) throw new RecoverableException("Pause before configuration");
            var fields = confidences.Split(',');
            var parsed = new int[fields.Length];
            if(!Plate(newPlate) || fields.Length < 1 || fields.Length > 16 ||
                delayTicks < 1 || delayTicks > 3000 || fragmentBytes < 1 || fragmentBytes > 128)
                throw new RecoverableException("Invalid camera configuration");
            for(var i = 0; i < parsed.Length; i++)
                if(!int.TryParse(fields[i], out parsed[i]) || parsed[i] < 0 || parsed[i] > 100)
                    throw new RecoverableException("Invalid confidence");
            plate = newPlate; confidence = parsed; captureIndex = 0;
            delay = delayTicks; fragment = fragmentBytes; cameraMissing = missing;
        }

        public void ConfigurePayment(int delayTicks, int fragmentBytes, string result, bool missing)
        {
            if(!machine.IsPaused || delayTicks < 1 || delayTicks > 3000 ||
                fragmentBytes < 1 || fragmentBytes > 128 ||
                (result != "PAID" && result != "CANCELLED" && result != "MALFORMED"))
                throw new RecoverableException("Invalid payment configuration");
            payDelay = delayTicks; payFragment = fragmentBytes;
            paymentResult = result; paymentMissing = missing;
        }

        public void ReplayPayment(string requestUuid, int delayTicks)
        {
            Charge charge;
            if(!machine.IsPaused || delayTicks < 1 || delayTicks > 3000 ||
                !charges.TryGetValue(requestUuid, out charge) || !charge.Delivered)
                throw new RecoverableException("Replay requires a retained completed request");
            Queue(false, Result(requestUuid, charge), delayTicks, payFragment, requestUuid);
        }

        public string Submit(int version, int run, int id, ulong atTick, string signal, int value)
            => timeline.Submit(version, run, id, atTick, signal, value);
        public int Run => timeline.Run;
        public ulong Tick => timeline.Tick;
        public string Trace => timeline.Trace;
        public string TraceAfter(ulong sequence) => timeline.TraceAfter(sequence);
        public int Pending => replies.Count + timeline.Pending;
        public ulong Dropped => timeline.Dropped;
        public int Requests { get; private set; }
        public int Captures { get; private set; }
        public int Payments { get; private set; }
        public int Errors { get; private set; }
        public int Replies { get; private set; }
        public string LastUuid => lastUuid;
        public string LastPayment => lastPayment;
        public string Snapshot => "{\"v\":1,\"run\":" + Run + ",\"tick\":" + Tick +
            ",\"time_us\":" + timeline.TimeMicroseconds + ",\"pending\":" + Pending +
            ",\"captures\":" + Captures + ",\"payments\":" + Payments +
            ",\"errors\":" + Errors + ",\"replies\":" + Replies +
            ",\"red\":" + (red ? "true" : "false") +
            ",\"green\":" + (green ? "true" : "false") +
            ",\"buzzer\":" + (buzzer ? "true" : "false") +
            ",\"dropped\":" + Dropped + "}";

        public void OnGPIO(int number, bool value)
        {
            bool old;
            switch(number)
            {
            case 0: old = red; red = value; break;
            case 1: old = green; green = value; break;
            case 2: old = buzzer; buzzer = value; break;
            default: throw new RecoverableException("Signal GPIO: red=0 green=1 buzzer=2");
            }
            if(old != value)
                timeline.Emit("commanded", "signals", number == 0 ? "red" :
                    number == 1 ? "green" : "buzzer", value ? 1 : 0);
        }

        private static bool Plate(string text) =>
            text != null && Regex.IsMatch(text, @"\A[A-Z]{3}[0-9][A-Z0-9][0-9]{2}\z");
        private static bool Uuid(string text) =>
            text != null && Regex.IsMatch(text,
                @"\A[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\z");
        private static bool CaptureId(string text) =>
            text != null && Regex.IsMatch(text, @"\A[1-9][0-9]{0,8}\z");

        private void Receive(StringBuilder input, byte value, bool cam)
        {
            timeline.Emit("commanded", cam ? "camera" : "payment", "tx_byte", value);
            bool discard = cam ? camDiscard : payDiscard;
            if(value == 10)
            {
                if(discard)
                {
                    if(!(cam ? camTimeout : payTimeout)) Error(cam, "FRAME");
                }
                else if(cam)
                {
                    if(input.Length == 0 || input[input.Length - 1] != '\r')
                        Error(true, "FRAME");
                    else Handle(input.ToString(0, input.Length - 1), true);
                }
                else Handle(input.ToString(), false);
                input.Clear();
                if(cam) { camDiscard = camTimeout = false; }
                else { payDiscard = payTimeout = false; }
                return;
            }
            if(discard) return;
            if((value < 32 && !(cam && value == 13)) || value > 126 || input.Length == 127)
            {
                input.Clear();
                if(cam) camDiscard = true; else payDiscard = true;
            }
            else
            {
                if(input.Length == 0)
                {
                    if(cam) camStarted = Tick; else payStarted = Tick;
                }
                input.Append((char)value);
            }
        }

        private void Handle(string frame, bool cam)
        {
            Requests++;
            if(cam)
            {
                if(frame == "AT")
                {
                    Queue(true, "OK\r\n", 1, fragment);
                    return;
                }
                const string prefix = "AT+CAPTURE=";
                if(!frame.StartsWith(prefix) || !CaptureId(frame.Substring(prefix.Length)))
                { Error(true, "FRAME"); return; }
                var id = frame.Substring(prefix.Length);
                string response;
                if(!captures.TryGetValue(id, out response))
                {
                    if(captures.Count == 64) { Error(true, "CAPACITY"); return; }
                    var c = confidence[Math.Min(captureIndex++, confidence.Length - 1)];
                    response = "+PLATE:" + id + ",\"" + plate + "\"," + c + "\r\n";
                    captures.Add(id, response);
                    Captures++;
                    timeline.Emit("physical", "camera", "confidence", c);
                }
                Queue(true, "OK\r\n", 1, fragment);
                if(!cameraMissing) Queue(true, response, delay, fragment);
                return;
            }
            var f = frame.Split(' ');
            if(f.Length < 3 || f[0] != "P1" || !Uuid(f[2]))
            { Error(false, "FRAME"); return; }
            Charge charge;
            if(f[1] == "CANCEL" && f.Length == 3)
            {
                if(!charges.TryGetValue(f[2], out charge))
                { Error(false, "UNKNOWN"); return; }
                // A completed PAID result cannot be undone by cancellation.
                if(!charge.Delivered)
                {
                    replies.RemoveAll(r => r.Key == f[2]);
                    charge.Result = "CANCELLED";
                }
                Queue(false, Result(f[2], charge), 1, payFragment, f[2]);
                return;
            }
            int amount;
            if(f.Length != 6 || f[1] != "CHARGE" || !Plate(f[3]) || f[5] != "BRL" ||
                !Regex.IsMatch(f[4], @"\A[1-9][0-9]{0,6}\z") ||
                !int.TryParse(f[4], out amount) || amount > 1000000)
            { Error(false, "FRAME"); return; }
            if(charges.TryGetValue(f[2], out charge))
            {
                if(charge.Plate != f[3] || charge.Amount != amount)
                { Error(false, "CONFLICT"); return; }
                if(replies.Exists(r => r.Key == f[2])) return;
            }
            else
            {
                if(charges.Count == 64) { Error(false, "CAPACITY"); return; }
                charge = new Charge { Plate = f[3], Amount = amount, Result = paymentResult };
                charges.Add(f[2], charge);
                Payments++;
                lastUuid = f[2];
                lastPayment = frame;
                timeline.Emit("physical", "payment", "charge_centavos", amount);
            }
            if(!paymentMissing) Queue(false, Result(f[2], charge), payDelay, payFragment, f[2]);
        }

        private string Result(string uuid, Charge charge) =>
            charge.Result == "MALFORMED" ? "P1 RESULT " + uuid + " PAID broken BRL\n" :
            "P1 RESULT " + uuid + " " + charge.Result + " " + charge.Amount + " BRL\n";

        private void Error(bool cam, string code)
        {
            Errors++;
            timeline.Emit("diagnostic", cam ? "camera" : "payment", "protocol_error", Errors);
            Queue(cam, cam ? "ERROR\r\n" : "P1 ERROR " + code + "\n", 1, 128);
        }

        private void Queue(bool cam, string text, int latency, int size, string key = "")
        {
            if(replies.Count >= 32)
            {
                Errors++;
                timeline.Emit("diagnostic", cam ? "camera" : "payment", "queue_full", 1);
                return;
            }
            replies.Add(new Reply { Camera = cam, Text = text, At = Tick + (ulong)latency,
                Size = size, Key = key });
        }

        private bool Validate(string signal, int value) =>
            (signal == "payment.replay" && value == 1) ||
            (signal == "payment.cancel" && value == 1);

        private void Execute(string signal, int value)
        {
            if(lastUuid.Length == 0) return;
            var charge = charges[lastUuid];
            if(signal == "payment.cancel" && !charge.Delivered)
            {
                replies.RemoveAll(r => r.Key == lastUuid);
                charge.Result = "CANCELLED";
            }
            Queue(false, Result(lastUuid, charge), 1, payFragment, lastUuid);
        }

        private void Step()
        {
            if(camInput.Length > 0 && Tick - camStarted >= 100)
            {
                camInput.Clear();
                camDiscard = camTimeout = true;
                Error(true, "TIMEOUT");
            }
            if(payInput.Length > 0 && Tick - payStarted >= 100)
            {
                payInput.Clear();
                payDiscard = payTimeout = true;
                Error(false, "TIMEOUT");
            }
            // Stable FIFO order; fragments never interleave on a UART.
            var blockedCamera = false;
            var blockedPayment = false;
            foreach(var reply in replies.ToArray())
            {
                if(reply.Camera ? blockedCamera : blockedPayment) continue;
                if(reply.Camera) blockedCamera = true; else blockedPayment = true;
                if(reply.At > Tick) continue;
                // Settlement is committed when transmission starts; a later
                // cancellation cannot truncate a frame already on the wire.
                if(reply.Key.Length != 0) charges[reply.Key].Delivered = true;
                var end = Math.Min(reply.Offset + reply.Size, reply.Text.Length);
                for(; reply.Offset < end; reply.Offset++)
                {
                    byte b = (byte)reply.Text[reply.Offset];
                    (reply.Camera ? camera : payment).WriteChar(b);
                    timeline.Emit("sensor", reply.Camera ? "camera" : "payment", "rx_byte", b);
                }
                reply.At = Tick + 1;
                if(reply.Offset != reply.Text.Length) continue;
                replies.Remove(reply);
                Replies++;
                if(reply.Key.Length != 0) charges[reply.Key].Delivered = true;
                timeline.Emit("physical", reply.Camera ? "camera" : "payment", "reply_complete", Replies);
            }
        }

        private sealed class Reply
        {
            public bool Camera;
            public string Text, Key;
            public ulong At;
            public int Offset, Size;
        }
        private sealed class Charge
        {
            public string Plate, Result;
            public int Amount;
            public bool Delivered;
        }
        private readonly IMachine machine;
        private readonly IUART camera, payment;
        private readonly EquipmentTimeline timeline;
        private readonly StringBuilder camInput = new StringBuilder(), payInput = new StringBuilder();
        private readonly List<Reply> replies = new List<Reply>();
        private readonly Dictionary<string, string> captures = new Dictionary<string, string>();
        private readonly Dictionary<string, Charge> charges = new Dictionary<string, Charge>();
        private bool camDiscard, payDiscard, camTimeout, payTimeout;
        private bool cameraMissing, paymentMissing, red, green, buzzer;
        private ulong camStarted, payStarted;
        private int[] confidence;
        private int captureIndex, delay, fragment, payDelay, payFragment;
        private string plate, paymentResult, lastPayment, lastUuid;
    }
}
