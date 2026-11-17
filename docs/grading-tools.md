# Grading tools

These host tools measure a firmware submission with the same models and
scenarios as the lab. Each takes the same `--module` and `--renode` as
[`lab.py`](setup.md#build-launch-and-inspect) and a **new** `--output`
directory beneath `build/`. They never modify `student/`. The Portuguese
[challenge brief](desafio.md) explains how the results become a grade; the
grade itself is computed by the instructor.

## Firmware line and branch coverage

```sh
python scripts/firmware-coverage.py --module "$ZVB_MODULE" \
  --renode "$RENODE_ROOT/renode" --output build/coverage-01
```

The tool builds `student/` (or `--app DIR`) for **gcov**, with
`CONFIG_DEBUG_OPTIMIZATIONS=y` and the 1000 ms tariff unit of the headless
fixtures. Only the application is instrumented (`-fprofile-arcs
-ftest-coverage`): the course module `scripts/grading/gcov_module/`, added to
coverage builds only, moves Zephyr's global coverage flags to the `app`
target, so the kernel and drivers keep their timing, and connects a spare
interrupt (IRQ 112) to Zephyr's `gcov_coverage_dump()`. It then runs every scenario
in its own Renode process (`--jobs`: by default up to 4, limited by the CPUs
and by about 2 GB of RAM per process). Virtual-time deadlines belong to each
process, so running them in parallel does not change any verdict.

At the end of a passing scenario, and before every machine reset, the
runtime pulses that interrupt and runs virtual time until the dump ends on
the console (a few kilobytes; the counters restart at each reset). A unit
suite dumps when its `main()` returns. The host writes each dump's `.gcda`
files beside copies of the build's `.gcno` files and reads them with the SDK's
`arm-zephyr-eabi-gcov` (JSON, `--branch-probabilities`).

- **Lines and branches.** A line is *executable* when gcov lists it, and
  *hit* when its count is positive. A **branch outcome** is one edge of a
  conditional jump that gcov reports for a line (an `if` has two; `a && b`
  has more); exception edges are left out. It is *taken* when its count is
  positive. Code the compiler or linker discarded is not counted. Unit-suite
  outcomes count only on lines where the suite build has the same number of
  outcomes as the firmware build, so that they mean the same edges.
- **Only passing scenarios and suites count.** A failing one still runs
  code, but that is not evidence of tested behaviour.
- **The host is never a verdict.** Each scenario gets 900 host seconds
  (`assess.py --wall-timeout`; the student default stays 120). One that runs
  out of host time before its end is a *timeout*; one whose simulator stops
  without a firmware failure (for example, killed when the host runs out of
  memory) is an *error*. Neither is a failure: each is retried once on its
  own, and if it persists it is reported as a host problem, with its lines
  not counted. Virtual-time deadlines are unaffected by host load.
- **Exclusions need a reason** and may cover at most 3% of executable lines;
  an excluded line takes its branch outcomes with it:
  `/* LCOV_EXCL_LINE: reason */`, or a
  `/* LCOV_EXCL_START: reason */` … `/* LCOV_EXCL_STOP */` block. A marker
  without a reason, an unterminated block, or exceeding the cap disables every
  exclusion. Use them for paths the models cannot reach, such as an ADC driver
  error, and expect to justify each one.
- `--exclude-branches-by-pattern REGEX` (alias `--exclude-lines-by-pattern`,
  repeatable) drops every source line whose whole text matches REGEX, with its
  branch outcomes, as gcovr does. For
  example `'\s*(LOG_[A-Z_]+\(|[A-Z0-9_]+_DEFINE\(|ZEPHLET_[A-Z_]+\().*'`
  removes logging calls and `*_DEFINE(` macros. These lines are reported as
  `excluded_by_pattern` and do not count toward the 3% marker cap.
- `--scenarios-dir DIR` adds trusted fixtures. The instructor uses it for the
  hidden set; a student's public-only figure is a lower bound of the graded
  one.

Outputs in the output directory:

| File | Content |
|---|---|
| `summary.json` | line and branch totals (`percent`, `branch_percent`), per-file figures with uncovered lines and branch outcomes (`line:index`), scenario statuses, notes |
| `coverage-map.json` | for each hit line, the scenarios that ran it (used by mutation testing) |
| `branch-map.json` | for each taken branch outcome, the scenarios that took it |
| `coverage.info` | LCOV with `DA` and `BRDA` records for editors or `genhtml --branch-coverage`; counts are scenarios |
| `scenarios/<name>/` | the assessment evidence of each scenario run |

`--min 95` makes the exit status 1 when line or branch coverage is below 95%.

### Unit-test suites

Every directory `<app>/tests/<suite>/` with a `CMakeLists.txt` is a ztest
application, normally using FFF (`zephyr/fff.h`) to fake the student's own
hardware layer. `student/tests/unit/` is a passing template: its CMake adds the
course board (`BOARD_ROOT`) and bindings (`DTS_ROOT`), its `Kconfig` sources the student
`Kconfig` (the tools pass `CONFIG_PARKING_*`), and it compiles chosen modules
from `../../src`, not `main.c`.

Each suite is built like the firmware (same board, coverage flags), prepared
with the same machine description, and run headless in Renode without a
monitor port (`-P -1`). USART1 goes to `console.log`, where the suite dumps
its gcov counters after ztest's summary. The runner stops Renode once both
`PROJECT EXECUTION` and the end of the dump are printed, which takes a few
seconds; the bound is 60 virtual seconds. A suite passes on
`PROJECT EXECUTION SUCCESSFUL` with no `FAIL` line. Its hits on `<app>/src`
lines and branch outcomes are merged under the name `unit:<suite>`. Lines and
outcomes that only the suite's binary contains (for example in a file the
firmware does not link) do not count: executable lines and branch outcomes
always come from the firmware build.

## Mutation score

Mutation testing is **feedback, not part of the grade**: a surviving mutant
shows a rule that no test checks.

```sh
python scripts/mutation-test.py --module "$ZVB_MODULE" \
  --renode "$RENODE_ROOT/renode" --output build/mutation-01 \
  --coverage build/coverage-01 --sample 60 --seed 1
```

The tool needs a coverage run of the same, unchanged sources: it refuses to
start when a file's hash differs. It generates deterministic mutants of
`<app>/src`, each changing one place:

| Operator | Change |
|---|---|
| ROR | `<` `<=` `>` `>=` `==` `!=` to a neighbour or the negation |
| LCR | `&&` ↔ `||` |
| AOR | `+` ↔ `-`, `/` → `*`, `%` → `/` |
| NEG | `if (c)` / `while (c)` → `if (!(c))` (not the `while` of a `do`) |
| CRP | integer constant: `0` → `1`, `1` → `0`, `n` → `n + 1` (not inside `[ ]` or a `= { … }` initializer such as a font table) |
| SDL | a call statement inside a function body is deleted |

Comments, strings and preprocessor lines are skipped, except the value of an
object-like `#define`. A mutant id, `<file>:<line>:<column>:<op>:<to>`, stays
the same while its line keeps its text.

`--sample N --seed S` evaluates N mutants (default 60; `0` means all), chosen
deterministically from the seed. Each of the `--jobs` workers owns a copy of
the application and a build directory that later mutants rebuild
incrementally. A mutant runs against the passing scenarios that cover its line,
shortest first, in one Renode process that stops at the first failure. Unit
suites that cover the line run first, since they take seconds:

- **killed**: a unit test failed or a scenario reported a firmware failure;
- **survived**: every covering scenario passed;
- **not covered**: no passing scenario reaches the line; it counts as a
  survivor and is not run;
- **invalid**: it does not compile; it does not count;
- **error**: host time ran out or the assessment stopped without a firmware
  failure (a harness problem). It does not count and is listed with its log
  in `logs/`. Re-run it with `--mutant <id>`, which evaluates only the given
  mutants.

Lines without a DWARF address (`#define` values, initializers) run against
every passing scenario. Declared equivalent mutants are listed in
`<app>/mutation-equivalent.json` as `[{"id": "...", "reason": "..."}]`. They
are removed only when they did not die and stay within 5% of the evaluated
mutants. An entry without a reason, or exceeding the cap, disables all of
them.

`score = killed / (killed + survived)`. The output directory holds
`summary.json`, `mutants.json` (status, scenarios and killer per mutant) and
the assessment log of every mutant that ran in `logs/`.
The surviving mutants are printed with their source line. `--min 90` makes
the exit status 1 below 90%.

## Static check

```sh
python scripts/static-check.py --module "$ZVB_MODULE" \
  --renode "$RENODE_ROOT/renode" --output build/static-01
```

This builds the application with Zephyr's GCC analyzer variant
(`ZEPHYR_SCA_VARIANT=gcc`, that is `-fanalyzer`) plus `-Wall -Wextra -Wshadow
-Wundef -Wdouble-promotion -Wformat=2 -Wcast-qual -Wstrict-prototypes
-Wmissing-prototypes -Wnull-dereference -Wswitch-enum`. The analyzer ships with
the pinned SDK, so nothing is installed. Zephyr is compiled with the same flags,
but only diagnostics located in `<app>/src` count, once per file, line and
flag. The exit status is 1 when there is any finding or the build fails.
