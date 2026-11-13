"""Host checks for static-check diagnostics filtering (no build)."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("static_check", ROOT / "scripts/static-check.py")
check = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(check)


class Findings(unittest.TestCase):
    def test_only_application_sources_count_once_each(self):
        with tempfile.TemporaryDirectory() as base:
            app = Path(base) / "app"
            (app / "src").mkdir(parents=True)
            main = app / "src/main.c"
            main.write_text("")
            log = "\n".join([
                f"{main}:12:5: warning: unused variable 'x' [-Wunused-variable]",
                f"{main}:12:5: warning: unused variable 'x' [-Wunused-variable]",
                f"{main}:30:13: warning: use after 'free' of 'p' [CWE-416] [-Wanalyzer-use-after-free]",
                f"{base}/zephyr/kernel/sched.c:7:1: warning: something [-Wshadow]",
                f"{app}/../app/src/main.c:40:2: error: expected ';' before '}}' token",
                "In file included from x.c:1:",
            ])
            found = check.findings(log, app)
        self.assertEqual([(f["file"], f["line"], f["flag"]) for f in found], [
            ("src/main.c", 12, "-Wunused-variable"),
            ("src/main.c", 30, "-Wanalyzer-use-after-free"),
            ("src/main.c", 40, None)])
        self.assertEqual(found[2]["severity"], "error")


if __name__ == "__main__":
    unittest.main()
