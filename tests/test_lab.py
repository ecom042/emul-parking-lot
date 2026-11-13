import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("lab", ROOT / "scripts/lab.py")
lab = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lab)


class LabPackagingTests(unittest.TestCase):
    def test_student_launch_reuses_build_assembly_and_restricted_web(self):
        build, prepare, launch = lab.commands(Path("/module"), Path("/renode"),
                                              Path("/output"))
        self.assertIn(str(ROOT / "student"), build)
        self.assertIn("nucleo_u5a5zj_q", build)
        self.assertIn(str(ROOT / "scripts/prepare-smoke.py"), prepare)
        self.assertIn(str(ROOT / "scripts/parking-web.py"), launch)
        self.assertNotIn("--allow-external-debug", launch)
        self.assertNotIn("zvb-web", launch)

    def test_headless_uses_private_transport_validator(self):
        _, _, launch = lab.commands(Path("/module"), Path("/renode"),
                                     Path("/output"), "normal")
        self.assertIn(str(ROOT / "scripts/assess.py"), launch)
        self.assertEqual(launch[-2:], ["--scenario", "normal"])
        self.assertNotIn("scenario-run.py", " ".join(launch))

    def test_build_selects_tariff_unit_and_optional_capacity(self):
        build, _, _ = lab.commands(Path("/module"), Path("/renode"), Path("/output"))
        self.assertEqual(build[-2:], ["--", "-DCONFIG_PARKING_TARIFF_UNIT_MS=5000"])
        build, _, _ = lab.commands(Path("/module"), Path("/renode"), Path("/output"),
                                   "normal", capacity=2)
        self.assertEqual(build[-3:], ["--", "-DCONFIG_PARKING_TARIFF_UNIT_MS=1000",
                                      "-DCONFIG_PARKING_CAPACITY=2"])

    def test_explicit_workspace_and_bundle_ignore_inherited_overrides(self):
        with patch.dict(lab.os.environ, {"ZVB_CONFIG": "/wrong.yml",
                                       "WEST_CONFIG_LOCAL": "/other/workspace",
                                       "PYTHONPATH": "/stale/source",
                                       "ZEPHYR_BASE": "/other/zephyr"}):
            env = lab.environment(Path("/reviewed"))
        self.assertNotIn("ZVB_CONFIG", env)
        self.assertEqual(env["WEST_CONFIG_LOCAL"], str(ROOT / ".west/config"))
        self.assertEqual(env["ZEPHYR_BASE"], str(ROOT / "deps/zephyr"))
        self.assertEqual(env["PYTHONPATH"], "/reviewed/src")
        _, prepare, _ = lab.commands(Path("/reviewed"), Path("/renode"),
                                      Path("/output"))
        self.assertEqual(prepare[-2:], [
            "--config", str(ROOT / "student/boards/zvb/nucleo_u5a5zj_q/zvb.yml")])

    def test_hashes_record_unpublished_bytes_not_cache(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "src/__pycache__").mkdir(parents=True)
            (root / "src/module.py").write_text("first")
            (root / "src/__pycache__/module.pyc").write_bytes(b"cache")
            first = lab.source_hashes(root, ["src"])
            (root / "src/module.py").write_text("second")
            self.assertEqual(list(first), ["src/module.py"])
            self.assertNotEqual(first, lab.source_hashes(root, ["src"]))

    def test_student_uses_public_hardware(self):
        self.assertEqual((ROOT / "student/app.overlay").read_text(),
                         '#include "../hardware/parking.overlay"\n')
        bundle = ROOT / "student/boards/zvb/nucleo_u5a5zj_q"
        self.assertIn("hardware/renode/parking.repl", (bundle / "zvb.yml").read_text())
        self.assertEqual((bundle / "../../../../hardware/renode/parking.repl").resolve(),
                         ROOT / "hardware/renode/parking.repl")
        self.assertTrue((ROOT / "hardware/renode/parking.repl").is_file())

    def test_reference_selection_is_not_a_student_cli_option(self):
        result = subprocess.run(
            [sys.executable, str(ROOT / "scripts/lab.py"),
             "--module", "/unused-module", "--renode", "/unused-runtime",
             "--output", str(ROOT / "build/not-created"),
             "--app", "reference"],
            capture_output=True, text=True, timeout=5,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("unrecognized arguments: --app reference", result.stderr)
