"""Host checks for the firmware line-coverage helpers (no Renode, no ELF)."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from grading import linecov  # noqa: E402


class ThumbLength(unittest.TestCase):
    def test_wide_prefixes(self):
        self.assertEqual(linecov.thumb_length(0xF380), 4)  # msr
        self.assertEqual(linecov.thumb_length(0xF44F), 4)  # mov.w
        self.assertEqual(linecov.thumb_length(0xE92D), 4)  # push.w
        self.assertEqual(linecov.thumb_length(0x480A), 2)  # ldr literal
        self.assertEqual(linecov.thumb_length(0xE7FE), 2)  # b . (0b11100 is narrow)


class HitsAndSummary(unittest.TestCase):
    RANGES = {("src/a.c", 1): [(0x100, 0x104)], ("src/a.c", 2): [(0x104, 0x108)],
              ("src/a.c", 3): [(0x108, 0x10A), (0x200, 0x204)], ("src/b.c", 9): [(0x300, 0x302)]}

    def test_instruction_start_inside_any_range_hits_the_line(self):
        hits = linecov.hit_lines(self.RANGES, {0x100, 0x202})
        self.assertEqual(hits, {("src/a.c", 1), ("src/a.c", 3)})

    def test_union_over_scenarios_and_line_map(self):
        summary, line_map = linecov.summarize(
            self.RANGES, {"one": {("src/a.c", 1)}, "two": {("src/a.c", 1), ("src/b.c", 9)}}, set())
        self.assertEqual((summary["executable"], summary["hit"], summary["percent"]), (4, 2, 50.0))
        self.assertEqual(summary["files"]["src/a.c"]["uncovered"], [2, 3])
        self.assertEqual(line_map["src/a.c"]["1"], ["one", "two"])

    def test_exclusions_within_cap_leave_the_count(self):
        ranges = {("src/a.c", n): [(n * 4, n * 4 + 2)] for n in range(1, 101)}
        hits = {("src/a.c", n) for n in range(1, 98)}
        summary, _ = linecov.summarize(ranges, {"s": hits}, {("src/a.c", 98), ("src/a.c", 99)})
        self.assertEqual((summary["executable"], summary["hit"], summary["excluded"]), (98, 97, 2))

    def test_exclusions_over_cap_are_ignored(self):
        ranges = {("src/a.c", n): [(n * 4, n * 4 + 2)] for n in range(1, 11)}
        summary, _ = linecov.summarize(ranges, {"s": set()}, {("src/a.c", 1)})
        self.assertEqual((summary["executable"], summary["excluded"]), (10, 0))
        self.assertIn("cap", summary["notes"][0])


class Markers(unittest.TestCase):
    def scan(self, text):
        with tempfile.TemporaryDirectory() as app:
            (Path(app) / "src").mkdir()
            (Path(app) / "src/x.c").write_text(text)
            return linecov.exclusions(app)

    def test_line_and_block_markers_need_reasons(self):
        excluded, problems = self.scan(
            "int a;\n"
            "f(); /* LCOV_EXCL_LINE: ADC driver fault not modelled */\n"
            "/* LCOV_EXCL_START: defensive default */\n"
            "g();\n"
            "/* LCOV_EXCL_STOP */\n"
            "h(); /* LCOV_EXCL_LINE */\n")
        self.assertEqual(excluded, {("src/x.c", n) for n in (2, 3, 4, 5, 6)})
        self.assertEqual(problems, ["src/x.c:6: LCOV_EXCL_LINE needs ': reason'"])

    def test_unterminated_block_is_reported(self):
        _, problems = self.scan("/* LCOV_EXCL_START: why */\nint a;\n")
        self.assertEqual(problems, ["src/x.c:1: LCOV_EXCL_START without LCOV_EXCL_STOP"])

    def test_problems_disable_every_exclusion(self):
        ranges = {("src/x.c", n): [(n * 4, n * 4 + 2)] for n in range(1, 101)}
        summary, _ = linecov.summarize(ranges, {"s": set()}, {("src/x.c", 1)},
                                       ["src/x.c:1: LCOV_EXCL_LINE needs ': reason'"])
        self.assertEqual(summary["excluded"], 0)


if __name__ == "__main__":
    unittest.main()
