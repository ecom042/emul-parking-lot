"""Host checks for mutant generation and the mutation score (no Renode)."""
import importlib.util
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from grading import mutants  # noqa: E402

SPEC = importlib.util.spec_from_file_location("mutation_test", ROOT / "scripts/mutation-test.py")
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)

SOURCE = """#include <stdio.h>
#define LIMIT 85
#define MAX(a, b) ((a) > (b) ? (a) : (b))
static char buffer[16];
static const unsigned char font[2][3] = {{0x3e, 0x41, 7}, {1, 2, 3}};
LOG_MODULE_REGISTER(app);
/* a < b in a comment */
int f(int x, const char *s)
{
\tif (x >= LIMIT && s[0] == 'A') {
\t\tnotify(x - 1);
\t}
\tdo { x++; } while (x < 3);
\tputs("a < b");
\treturn -1;
}
"""


class Generation(unittest.TestCase):
    def setUp(self):
        self.found = mutants.mutants("src/x.c", SOURCE)
        self.by_id = {m.id: m for m in self.found}

    def lines(self, mutant):
        return mutant.apply(SOURCE).splitlines()

    def test_relational_logical_arithmetic_and_negation(self):
        ops = {(m.op, m.line, m.replacement) for m in self.found}
        for expected in [("ROR", 10, ">"), ("ROR", 10, "<"), ("ROR", 10, "!="), ("LCR", 10, "||"),
                         ("AOR", 11, "+"), ("AOR", 15, "+"), ("NEG", 10, "if!"),
                         ("ROR", 13, "<="), ("CRP", 2, "86"), ("CRP", 11, "0"), ("SDL", 11, "deleted"),
                         ("SDL", 14, "deleted")]:
            self.assertIn(expected, ops)

    def test_skips_comments_strings_includes_macros_and_array_sizes(self):
        lines = {m.line for m in self.found}
        self.assertFalse(lines & {1, 3, 5, 7}, lines)
        self.assertNotIn(("CRP", 4), {(m.op, m.line) for m in self.found})
        self.assertNotIn(("ROR", 14), {(m.op, m.line) for m in self.found})

    def test_file_scope_macros_and_do_while_are_not_mutated(self):
        self.assertFalse([m for m in self.found if m.line == 6])
        self.assertFalse([m for m in self.found if m.op == "NEG" and m.line == 13])

    def test_edits_produce_the_expected_text(self):
        negated = self.by_id["src/x.c:10:2:NEG:if!"]
        self.assertEqual(self.lines(negated)[9], "\tif (!(x >= LIMIT && s[0] == 'A')) {")
        deleted = self.by_id["src/x.c:11:3:SDL:deleted"]
        self.assertEqual(self.lines(deleted)[10], "\t\t;")
        constant = next(m for m in self.found if m.op == "CRP" and m.line == 2)
        self.assertEqual(self.lines(constant)[1], "#define LIMIT 86")

    def test_initializer_tables_are_not_mutated(self):
        self.assertFalse([m for m in self.found if m.line == 5])

    def test_ids_are_unique_and_stable(self):
        self.assertEqual(len(self.by_id), len(self.found))
        self.assertEqual([m.id for m in mutants.mutants("src/x.c", SOURCE)],
                         [m.id for m in self.found])


class Scoring(unittest.TestCase):
    def results(self, *statuses):
        return [{"id": f"m{i}", "status": s} for i, s in enumerate(statuses)]

    def test_invalid_mutants_do_not_count(self):
        figures = runner.score(self.results("killed", "killed", "survived", "invalid",
                                            "not_covered"), {})
        self.assertEqual((figures["killed"], figures["survived"], figures["invalid"]), (2, 2, 1))
        self.assertEqual(figures["score"], 50.0)

    def test_declared_equivalents_within_cap_are_removed(self):
        statuses = ["killed"] * 19 + ["survived"]
        figures = runner.score(self.results(*statuses), {"m19": "same behaviour"})
        self.assertEqual((figures["equivalent"], figures["score"]), (1, 100.0))

    def test_equivalents_over_cap_are_ignored(self):
        figures = runner.score(self.results("killed", "survived"), {"m1": "same"})
        self.assertEqual((figures["equivalent"], figures["score"]), (0, 50.0))
        self.assertIn("cap", figures["notes"][0])

    def test_verdicts(self):
        passing = {"name": "a", "finished": True, "failure": None}
        failing = {"name": "b", "finished": False,
                   "failure": {"kind": "firmware_failure", "message": "checkpoint"}}
        harness = {"name": "c", "finished": False,
                   "failure": {"kind": "harness_failure", "message": "transport"}}
        self.assertEqual(runner.verdict(0, [passing])["status"], "survived")
        self.assertEqual(runner.verdict(1, [passing, failing]),
                         {"status": "killed", "killed_by": "b"})
        self.assertEqual(runner.verdict(-9, [passing])["killed_by"], "timeout")
        self.assertEqual(runner.verdict(1, [harness])["status"], "error")
        self.assertEqual(runner.verdict(1, [])["status"], "error")

    def test_sample_is_deterministic(self):
        items = list(range(100))
        self.assertEqual(runner.choose(items, 10, 7), runner.choose(items, 10, 7))
        self.assertNotEqual(runner.choose(items, 10, 7), runner.choose(items, 10, 8))
        self.assertEqual(runner.choose(items, 0, 1), items)

    def test_plan_runs_covering_passing_scenarios_shortest_first(self):
        coverage = {"scenarios": {"a": "pass", "b": "pass", "c": "fail"},
                    "scenario_elapsed_s": {"a": 30, "b": 10, "c": 5},
                    "map": {"src/x.c": {"9": ["a", "b", "c"]}}}
        lines = {("src/x.c", 9), ("src/x.c", 10)}
        mutant = mutants.Mutant("id", "src/x.c", 9, "ROR", (), "<", "<=")
        self.assertEqual(runner.plan(mutant, coverage, lines), ["b", "a"])
        uncovered = mutants.Mutant("id", "src/x.c", 10, "ROR", (), "<", "<=")
        self.assertEqual(runner.plan(uncovered, coverage, lines), [])
        unmapped = mutants.Mutant("id", "src/x.c", 2, "CRP", (), "85", "86")
        self.assertEqual(runner.plan(unmapped, coverage, lines), ["b", "a"])


if __name__ == "__main__":
    unittest.main()
