# Grading tools

These host tools measure a firmware submission with the same models and
scenarios as the lab. Each takes the same `--module` and `--renode` as
[`lab.py`](setup.md#build-launch-and-inspect) and a **new** `--output`
directory beneath `build/`. They never modify `student/`. The Portuguese
[challenge brief](desafio.md) explains how the results become a grade.

## Firmware line coverage

```sh
python scripts/firmware-coverage.py --module "$ZVB_MODULE" \
  --renode "$RENODE_ROOT/renode" --output build/coverage-01
```

The tool builds `student/` (or `--app DIR`) with
`CONFIG_DEBUG_OPTIMIZATIONS=y` so that lines map to code predictably, and
the 1000 ms tariff unit of the headless fixtures. It then runs every scenario
in its own Renode process (`--jobs`, default 4). Virtual-time deadlines belong
to each process, so running them in parallel does not change any verdict.

During those runs `models/BlockCoverage.cs` records each distinct translation
block the CPU starts (address and instruction count) once. That is kilobytes
per scenario, where a full PC trace is hundreds of megabytes. The block lists
are mapped to lines of `<app>/src/*.c|h` through the ELF's DWARF line table,
then deleted.

- **Line coverage, not branch coverage.** A line is *executable* when the
  line table gives it an address, and *hit* when an executed instruction
  starts in its range. A line with two outcomes counts as hit once either one
  runs. Code the linker discarded has no address and is not counted.
- **Only passing scenarios count.** A failing scenario still runs code, but
  that is not evidence of tested behaviour.
- **Exclusions need a reason** and may cover at most 3% of executable lines:
  `/* LCOV_EXCL_LINE: reason */`, or a
  `/* LCOV_EXCL_START: reason */` … `/* LCOV_EXCL_STOP */` block. A marker
  without a reason, an unterminated block, or exceeding the cap disables every
  exclusion. Use them for paths the models cannot reach, such as an ADC driver
  error, and expect to justify each one.
- `--scenarios-dir DIR` adds trusted fixtures. The instructor uses it for the
  hidden set; a student's public-only figure is a lower bound of the graded
  one.

Outputs in the output directory:

| File | Content |
|---|---|
| `summary.json` | totals, per-file figures and uncovered lines, scenario statuses, notes |
| `coverage-map.json` | for each hit line, the scenarios that ran it (used by mutation testing) |
| `coverage.info` | LCOV for editors or `genhtml`; a line's count is the number of scenarios that hit it |
| `scenarios/<name>/` | the assessment evidence of each scenario run |

`--min 95` makes the exit status 1 below 95%.
