"""Bounded source archive tests: no Renode, installation, or network."""

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile


SCRIPT = Path(__file__).resolve().parents[1] / "scripts/package-lab.py"
SPEC = importlib.util.spec_from_file_location("package_lab", SCRIPT)
packager = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(packager)


class AllowlistTests(unittest.TestCase):
    def test_every_scenario_module_ships_to_students(self):
        root = SCRIPT.parents[1]
        modules = {p.relative_to(root / "scripts").as_posix()
                   for p in (root / "scripts/scenario").glob("*.py")}
        self.assertEqual(modules - packager.STUDENT_SCRIPTS, set())
        tests = {p.name for p in (root / "tests").glob("test_*")}
        self.assertEqual(tests - packager.STUDENT_TESTS, set())


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name).resolve()
        self.parking = self.base / "parking"
        self.module = self.base / "module"
        self.parking.mkdir()
        self.module.mkdir()
        self.write(self.parking, "README.md", "course\n")
        self.write(self.parking, "app/src/main.c", "int main(void) { return 0; }\n")
        self.write(self.parking, "app/CMakeLists.txt", "project(parking)\n")
        self.write(self.parking, "reference/src/main.c", "/* instructor solution marker */\n")
        self.write(self.parking, "probes/io/src/main.c", "/* solution probe marker */\n")
        self.write(self.parking, "student/src/main.c", "/* student implementation */\n")
        self.write(self.parking, "student/CMakeLists.txt", "project(student)\n")
        self.write(self.parking, "hardware/parking.overlay", "/* hardware configuration */\n")
        self.write(self.parking, "docs/setup.md", "# Setup\n")
        self.write(self.parking, "docs/release-checklist.md", "# Release checklist\n")
        self.write(self.module, "src/zvb/_resc.py", "# module\n")
        self.write(self.module, "scripts/west-commands.yml", "west-commands: []\n")
        self.write(self.module, "LICENSE", "fixture license text\n")
        self.write(self.module, "NOTICE", "fixture attribution\n")

    def write(self, root, relative, text):
        target = root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")
        return target

    def package(self, output="build/pilot"):
        return packager.package(self.module, output, root=self.parking)

    def test_roundtrip_manifest_hashes_student_sources_and_legal_files(self):
        manifest = self.package()
        output = self.parking / "build/pilot"
        self.assertEqual(json.loads((output / "manifest.json").read_text()), manifest)
        self.assertTrue(any("emul-parking-lot: root license missing" in blocker
                            for blocker in manifest["release_blockers"]))
        self.assertFalse(any("zephyr-virtual-board:" in blocker
                             for blocker in manifest["release_blockers"]))
        for name, source in manifest["sources"].items():
            archive = output / source["archive"]
            self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(),
                             source["archive_sha256"])
            with zipfile.ZipFile(archive) as bundle:
                self.assertEqual(set(bundle.namelist()),
                                 {f["archive_path"] for f in source["files"]})
                for entry in source["files"]:
                    self.assertEqual(entry["archive_path"], f"{name}/{entry['path']}")
                    data = bundle.read(entry["archive_path"])
                    self.assertEqual(len(data), entry["size"])
                    self.assertEqual(hashlib.sha256(data).hexdigest(), entry["sha256"])
                bundle.extractall(self.base / "extracted")
        extracted = self.base / "extracted"
        self.assertEqual(manifest["audience"], "student")
        for directory in ("app", "reference", "probes", "instructor"):
            self.assertFalse((extracted / "emul-parking-lot" / directory).exists())
        for relative in ("student/src/main.c", "student/CMakeLists.txt",
                         "hardware/parking.overlay", "docs/setup.md",
                         "docs/release-checklist.md"):
            with self.subTest(relative=relative):
                self.assertEqual(
                    (extracted / "emul-parking-lot" / relative).read_bytes(),
                    (self.parking / relative).read_bytes(),
                )
        self.assertEqual((extracted / "zephyr-virtual-board/LICENSE").read_text(),
                         "fixture license text\n")
        self.assertTrue((extracted / "zephyr-virtual-board/NOTICE").is_file())
        self.assertFalse((extracted / "emul-parking-lot/.git").exists())

    def test_instructor_material_is_excluded_even_if_present_in_working_tree(self):
        private = [
            "reference/src/controller.c", "probes/adc/src/main.c",
            "instructor/solution.c", "instructor-local/reference/src/main.c",
            "student/instructor-local/solution.c", "docs/instructor-guide.md",
            "tests/native/distance_lifetime.c", "tests/parking.robot",
            "tests/test_distance_lifetime.py", "tests/unknown_solution.py",
            "scripts/validate-phase5-browser.py", "scripts/unknown_solution.py",
        ]
        for relative in private:
            self.write(self.parking, relative, "INSTRUCTOR_SOLUTION_MARKER\n")
        public = [
            "scripts/scenario/observations.py", "tests/test_scenario.py",
            "tests/scenario.robot", "tests/scenarios/normal.json",
        ]
        for relative in public:
            self.write(self.parking, relative, "public infrastructure\n")
        source = self.package()["sources"]["emul-parking-lot"]
        paths = {entry["path"] for entry in source["files"]}
        self.assertFalse(paths.intersection(private))
        self.assertTrue(set(public).issubset(paths))
        with zipfile.ZipFile(self.parking / "build/pilot" / source["archive"]) as archive:
            for name in archive.namelist():
                self.assertNotIn(b"INSTRUCTOR_SOLUTION_MARKER", archive.read(name))

    def test_allowlist_cache_secret_binary_and_symlink_exclusions(self):
        forbidden = [
            "build/generated.py", "deps/dependency.py", ".git/config",
            ".venv/tool.py", "unapproved/source.py", "student/build/generated.c",
            "student/deps/dependency.c", "student/.venv/tool.py", "student/__pycache__/cache.py",
            "student/node_modules/code.js", "student/secrets/password.py",
            "student/.env", "student/.env.json", "student/credentials.json", "student/token.json",
            "student/passwords.txt", "student/.cache/data.json",
            "student/server.key", "student/firmware.elf", "student/image.bin",
        ]
        for root in (self.parking, self.module):
            for relative in forbidden:
                self.write(root, relative, "excluded\n")
        self.write(self.module, "src/__pycache__/cache.py", "excluded\n")
        self.write(self.module, "src/secret.json", "excluded\n")
        (self.parking / "student/link.c").symlink_to(self.parking / "student/src/main.c")
        (self.parking / "student/linked").symlink_to(self.module, target_is_directory=True)
        (self.module / "src/zvb/link.py").symlink_to(self.module / "src/zvb/_resc.py")
        manifest = self.package()
        for name, source in manifest["sources"].items():
            paths = {f["path"] for f in source["files"]}
            self.assertFalse(paths.intersection(forbidden), name)
            self.assertFalse(any("link" in p or "__pycache__" in p for p in paths))
            self.assertNotIn("src/secret.json", paths)

    def test_output_must_be_new_and_beneath_build_without_symlinks(self):
        self.package()
        before = (self.parking / "build/pilot/manifest.json").read_bytes()
        for output in ("build/pilot", "build", "elsewhere/pilot", "../outside",
                       str(self.base / "outside")):
            with self.subTest(output=output), self.assertRaises(ValueError):
                self.package(output)
        (self.parking / "build/linked").symlink_to(self.base, target_is_directory=True)
        with self.assertRaises(ValueError):
            self.package("build/linked/output")
        self.assertEqual((self.parking / "build/pilot/manifest.json").read_bytes(), before)
        self.assertFalse((self.base / "output").exists())

    def test_archives_repeat_exact_bytes_without_packaging_prior_outputs(self):
        first = self.package("build/first")
        second = self.package("build/second")
        for name in first["sources"]:
            self.assertEqual(first["sources"][name]["archive_sha256"],
                             second["sources"][name]["archive_sha256"])

    @unittest.skipUnless(shutil.which("git"), "Git unavailable")
    def test_unborn_git_head_is_null_not_a_fabricated_commit(self):
        subprocess.run(["git", "init", "-q", str(self.parking)], check=True,
                       capture_output=True, timeout=5)
        base = self.package()["sources"]["emul-parking-lot"]["source_base_git"]
        self.assertIsNone(base["commit"])
        self.assertEqual(base["status"], "unavailable_or_unborn")

    @unittest.skipUnless(shutil.which("git"), "Git unavailable")
    def test_available_git_base_is_recorded_with_dirty_state(self):
        subprocess.run(["git", "init", "-q", str(self.module)], check=True,
                       capture_output=True, timeout=5)
        for args in (["add", "."], ["-c", "user.name=Fixture",
                                   "-c", "user.email=fixture@example.invalid",
                                   "-c", "commit.gpgsign=false", "commit", "-qm", "Fixture"]):
            subprocess.run(["git", "-C", str(self.module), *args], check=True,
                           capture_output=True, timeout=5,
                           env={**os.environ, "GIT_EDITOR": "true"})
        expected = subprocess.check_output(
            ["git", "-C", str(self.module), "rev-parse", "HEAD"], text=True,
            timeout=5,
        ).strip()
        self.write(self.module, "src/zvb/_resc.py", "# changed local source\n")
        base = self.package()["sources"]["zephyr-virtual-board"]["source_base_git"]
        self.assertEqual(base, {"commit": expected, "dirty": True, "status": "available"})

    @unittest.skipUnless(shutil.which("git"), "Git unavailable")
    def test_student_manifest_does_not_point_back_to_instructor_history(self):
        subprocess.run(["git", "init", "-q", str(self.parking)], check=True,
                       capture_output=True, timeout=5)
        for args in (["add", "."], ["-c", "user.name=Fixture",
                                   "-c", "user.email=fixture@example.invalid",
                                   "-c", "commit.gpgsign=false", "commit", "-qm", "Fixture"]):
            subprocess.run(
                ["git", "-C", str(self.parking), *args], check=True,
                capture_output=True, timeout=5, env={**os.environ, "GIT_EDITOR": "true"},
            )
        base = self.package()["sources"]["emul-parking-lot"]["source_base_git"]
        self.assertIsNone(base["commit"])
        self.assertEqual(base["status"], "instructor_history_excluded")

    def test_packager_accepts_extracted_source_without_git(self):
        self.package()
        extracted = self.base / "clean-pilot"
        for archive in (self.parking / "build/pilot").glob("*.zip"):
            with zipfile.ZipFile(archive) as bundle:
                bundle.extractall(extracted)
        manifest = packager.package(extracted / "zephyr-virtual-board",
                                    "build/repacked", root=extracted / "emul-parking-lot")
        for source in manifest["sources"].values():
            self.assertIsNone(source["source_base_git"]["commit"])

    def test_cli_creates_only_local_archives_and_reports_license_blocker(self):
        script = self.parking / "scripts/package-lab.py"
        script.parent.mkdir()
        shutil.copyfile(SCRIPT, script)
        result = subprocess.run(
            [sys.executable, str(script), "--module", str(self.module),
             "--output", "build/cli"], cwd=self.base, check=True,
            capture_output=True, text=True, timeout=10,
        )
        self.assertIn("RELEASE BLOCKER: emul-parking-lot: root license missing",
                      result.stdout)
        self.assertEqual(
            {p.name for p in (self.parking / "build/cli").iterdir()},
            {"emul-parking-lot.zip", "zephyr-virtual-board.zip", "manifest.json"},
        )

    def test_symlinked_module_source_is_not_accepted_as_complete(self):
        resc = self.module / "src/zvb/_resc.py"
        resc.unlink()
        resc.symlink_to(self.parking / "README.md")
        with self.assertRaisesRegex(ValueError, "non-symlink"):
            self.package()
        self.assertFalse((self.parking / "build/pilot").exists())


if __name__ == "__main__":
    unittest.main()
