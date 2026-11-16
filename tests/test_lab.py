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

    @staticmethod
    def workspace(root, version=(4, 5, 0)):
        """A topology-2 workspace: .west/, zephyr/ of `version` and the lab; returns the lab."""
        (root / ".west").mkdir(parents=True)
        (root / ".west/config").write_text(
            "[manifest]\npath = emul-parking-lot\nfile = west.yml\n\n[zephyr]\nbase = zephyr\n")
        (root / "zephyr").mkdir()
        (root / "zephyr/VERSION").write_text(
            "VERSION_MAJOR = {}\nVERSION_MINOR = {}\nPATCHLEVEL = {}\n".format(*version))
        (root / "emul-parking-lot").mkdir()
        return root / "emul-parking-lot"

    def test_zephyr_comes_from_the_labs_own_workspace(self):
        with tempfile.TemporaryDirectory() as temporary:
            topdir = Path(temporary).resolve() / "estacionamento"
            lab_root = self.workspace(topdir)
            with patch.object(lab, "ROOT", lab_root), \
                    patch.dict(lab.os.environ, {"ZVB_CONFIG": "/wrong.yml",
                                                "WEST_CONFIG_LOCAL": "/other/workspace",
                                                "PYTHONPATH": "/stale/source",
                                                "ZEPHYR_BASE": "/other/zephyr"}):
                env = lab.environment(Path("/reviewed"))
        self.assertNotIn("ZVB_CONFIG", env)
        self.assertEqual(env["WEST_CONFIG_LOCAL"], str(topdir / ".west/config"))
        self.assertEqual(env["ZEPHYR_BASE"], str(topdir / "zephyr"))
        self.assertEqual(env["PYTHONPATH"], "/reviewed/src")
        _, prepare, _ = lab.commands(Path("/reviewed"), Path("/renode"),
                                      Path("/output"))
        self.assertEqual(prepare[-2:], [
            "--config", str(ROOT / "student/boards/zvb/nucleo_u5a5zj_q/zvb.yml")])

    def test_workspace_must_exist_and_pin_the_zephyr_version(self):
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary).resolve()
            with patch.object(lab, "ROOT", temporary / "loose/emul-parking-lot"):
                self.assertIn("west init -l emul-parking-lot", lab.workspace()[2])
            old = self.workspace(temporary / "old", (4, 4, 0))
            with patch.object(lab, "ROOT", old):
                self.assertIn("has Zephyr v4.4.0", lab.workspace()[2])
                with self.assertRaises(SystemExit):
                    lab.environment(Path("/reviewed"))
            empty = self.workspace(temporary / "empty")
            (empty.parent / "zephyr/VERSION").unlink()
            with patch.object(lab, "ROOT", empty):
                self.assertIn("run west update", lab.workspace()[2])

    def test_manifest_is_topology_2(self):
        text = (ROOT / "west.yml").read_text()
        self.assertIn("path: emul-parking-lot", text)
        self.assertIn("path: zephyr\n", text)
        self.assertIn("path: zephyr-virtual-board\n", text)
        self.assertIn("# Zephyr v4.5.0", text)

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
