"""Host checks for the coverage helpers (no Renode, no gcov run)."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from grading import gcov, linecov  # noqa: E402


class Dumps(unittest.TestCase):
    def test_records_of_complete_dumps_only(self):
        text = ("boot\nGCOV_COVERAGE_DUMP_START\n*/b/app.dir/src/a.c.gcda<0a0b\n"
                "*/b/app.dir/src/b.c.gcda<ff\nGCOV_COVERAGE_DUMP_END\nlog\n"
                "GCOV_COVERAGE_DUMP_START\n*/b/app.dir/src/a.c.gcda<01\n")
        self.assertEqual(gcov.parse_dumps(text), [{"/b/app.dir/src/a.c.gcda": b"\x0a\x0b",
                                                   "/b/app.dir/src/b.c.gcda": b"\xff"}])

    def test_odd_hex_is_rejected(self):
        with self.assertRaises(ValueError):
            gcov.parse_dumps("GCOV_COVERAGE_DUMP_START*/x.gcda<abc\nGCOV_COVERAGE_DUMP_END")

    def test_gcov_json_keeps_app_sources_and_drops_exception_edges(self):
        with tempfile.TemporaryDirectory() as app:
            app = Path(app).resolve()
            (app / "src").mkdir()
            document = {"current_working_directory": str(app / "build"), "files": [
                {"file": str(app / "src/a.c"), "lines": [
                    {"line_number": 3, "count": 2, "branches": [
                        {"count": 2, "throw": False}, {"count": 0, "throw": False},
                        {"count": 0, "throw": True}]},
                    {"line_number": 4, "count": 0, "branches": []}]},
                {"file": "/zephyr/kernel/sched.c", "lines": [
                    {"line_number": 1, "count": 9, "branches": []}]}]}
            lines, branches = gcov._read(app, [document, document])
        self.assertEqual(lines, {("src/a.c", 3): 4, ("src/a.c", 4): 0})
        self.assertEqual(branches, {("src/a.c", 3, 0): 4, ("src/a.c", 3, 1): 0})
        self.assertEqual(gcov.hits(branches), {("src/a.c", 3, 0)})

    def test_suite_branches_count_only_where_the_builds_agree(self):
        firmware = {("src/a.c", 3, 0), ("src/a.c", 3, 1), ("src/a.c", 7, 0), ("src/a.c", 7, 1)}
        suite = {("src/a.c", 3, 0), ("src/a.c", 3, 1),
                 ("src/a.c", 7, 0), ("src/a.c", 7, 1), ("src/a.c", 7, 2)}
        self.assertEqual(gcov.compatible_branches(firmware, suite),
                         {("src/a.c", 3, 0), ("src/a.c", 3, 1)})


class Summary(unittest.TestCase):
    LINES = {("src/a.c", 1), ("src/a.c", 2), ("src/a.c", 3), ("src/b.c", 9)}
    BRANCHES = {("src/a.c", 2, 0), ("src/a.c", 2, 1), ("src/b.c", 9, 0), ("src/b.c", 9, 1)}

    def test_union_over_scenarios_and_maps(self):
        summary, line_map, branch_map = linecov.summarize(
            self.LINES, {"one": {("src/a.c", 1)}, "two": {("src/a.c", 1), ("src/b.c", 9)}}, set(),
            branches=self.BRANCHES,
            branch_hits={"one": {("src/a.c", 2, 1)}, "two": {("src/b.c", 9, 0)}})
        self.assertEqual((summary["executable"], summary["hit"], summary["percent"]), (4, 2, 50.0))
        self.assertEqual((summary["branches"], summary["branches_hit"], summary["branch_percent"]),
                         (4, 2, 50.0))
        self.assertEqual(summary["files"]["src/a.c"]["uncovered"], [2, 3])
        self.assertEqual(summary["files"]["src/a.c"]["uncovered_branches"], ["2:0"])
        self.assertEqual(line_map["src/a.c"]["1"], ["one", "two"])
        self.assertEqual(branch_map["src/b.c"]["9:0"], ["two"])

    def test_an_excluded_line_drops_its_branches(self):
        lines = {("src/a.c", n) for n in range(1, 101)}
        branches = {("src/a.c", 50, 0), ("src/a.c", 50, 1), ("src/a.c", 60, 0), ("src/a.c", 60, 1)}
        summary, _, _ = linecov.summarize(lines, {"s": set()}, {("src/a.c", 50)},
                                          branches=branches, branch_hits={"s": set()})
        self.assertEqual((summary["excluded"], summary["branches"]), (1, 2))

    def test_no_branches_is_full_branch_coverage(self):
        summary, _, _ = linecov.summarize({("src/a.c", 1)}, {"s": {("src/a.c", 1)}}, set())
        self.assertEqual(summary["branch_percent"], 100.0)

    def test_exclusions_within_cap_leave_the_count(self):
        lines = {("src/a.c", n) for n in range(1, 101)}
        hits = {("src/a.c", n) for n in range(1, 98)}
        summary, _, _ = linecov.summarize(lines, {"s": hits}, {("src/a.c", 98), ("src/a.c", 99)})
        self.assertEqual((summary["executable"], summary["hit"], summary["excluded"]), (98, 97, 2))

    def test_exclusions_over_cap_are_ignored(self):
        lines = {("src/a.c", n) for n in range(1, 11)}
        summary, _, _ = linecov.summarize(lines, {"s": set()}, {("src/a.c", 1)})
        self.assertEqual((summary["executable"], summary["excluded"]), (10, 0))
        self.assertIn("cap", summary["notes"][0])

    def test_lcov_has_lines_and_branches(self):
        summary, line_map, branch_map = linecov.summarize(
            self.LINES, {"one": {("src/a.c", 1)}}, set(), branches=self.BRANCHES,
            branch_hits={"one": {("src/a.c", 2, 1)}})
        with tempfile.TemporaryDirectory() as out:
            linecov.write_lcov(Path(out) / "c.info", "/app", summary, line_map, branch_map)
            text = (Path(out) / "c.info").read_text()
        self.assertIn("DA:1,1\nDA:2,0", text)
        self.assertIn("BRDA:2,0,0,0\nBRDA:2,0,1,1\nBRH:1\nBRF:2", text)


class Patterns(unittest.TestCase):
    PATTERN = r"\s*(LOG_[A-Z_]+\(|[A-Z0-9_]+_DEFINE\(|ZEPHLET_[A-Z_]+\().*"

    def test_logging_and_define_macros_are_dropped_outside_the_cap(self):
        with tempfile.TemporaryDirectory() as app:
            (Path(app) / "src").mkdir()
            (Path(app) / "src/x.c").write_text(
                "K_THREAD_DEFINE(t, 512, f, NULL, NULL, NULL, 5, 0, 0);\n"
                "\tLOG_INF(\"open %d\", lane);\n"
                "\tZEPHLET_CALL(x);\n"
                "\tmy_LOG_INF(1);\n"
                "\trc = gpio_pin_set_dt(&spec, 1);\n")
            dropped = linecov.pattern_exclusions(app, [self.PATTERN])
        self.assertEqual(dropped, {("src/x.c", 1), ("src/x.c", 2), ("src/x.c", 3)})
        lines = {("src/x.c", n) for n in range(1, 6)}
        summary, _, _ = linecov.summarize(lines, {"s": {("src/x.c", 5)}}, set(),
                                          by_pattern=dropped)
        self.assertEqual((summary["executable"], summary["hit"], summary["excluded_by_pattern"],
                          summary["excluded"]), (2, 1, 3, 0))


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
        lines = {("src/x.c", n) for n in range(1, 101)}
        summary, _, _ = linecov.summarize(lines, {"s": set()}, {("src/x.c", 1)},
                                       ["src/x.c:1: LCOV_EXCL_LINE needs ': reason'"])
        self.assertEqual(summary["excluded"], 0)


if __name__ == "__main__":
    unittest.main()
