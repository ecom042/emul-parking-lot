// SPDX-License-Identifier: Apache-2.0
// Course coverage helper: records each distinct translation block the CPU
// starts (address and instruction count) once, instead of a full PC trace.
// Loaded only by coverage runs (scripts/coverage.py); never by the student lab.
using System;
using System.Collections.Generic;
using System.IO;
using Antmicro.Renode.Exceptions;
using Antmicro.Renode.Peripherals.CPU;

namespace Antmicro.Renode.Course
{
    public static class BlockCoverageExtensions
    {
        // Monitor usage: sysbus.cpu0 StartBlockCoverage @/path/blocks.txt
        // Each call replaces the previous hook and starts a new file.
        public static void StartBlockCoverage(this TranslationCPU cpu, string path)
        {
            if(writers.TryGetValue(cpu, out var previous))
            {
                previous.Dispose();
            }
            StreamWriter writer;
            try
            {
                writer = new StreamWriter(path, false) { AutoFlush = true };
            }
            catch(Exception e)
            {
                throw new RecoverableException($"cannot open coverage file: {e.Message}");
            }
            writers[cpu] = writer;
            var seen = new HashSet<(ulong, uint)>();
            // Unique blocks are few, so each is written once and flushed: the
            // file stays complete even when Renode is stopped without notice.
            cpu.SetHookAtBlockBegin((address, size) =>
            {
                if(seen.Add((address, size)))
                {
                    writer.WriteLine($"{address:x} {size}");
                }
            });
        }

        private static readonly Dictionary<TranslationCPU, StreamWriter> writers =
            new Dictionary<TranslationCPU, StreamWriter>();
    }
}
