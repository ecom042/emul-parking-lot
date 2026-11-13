"""Create student source archives with curated infrastructure and no instructor solution."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import zipfile


ROOT = Path(__file__).resolve().parents[1]
DIRECTORIES = {
    "emul-parking-lot": {
        "docs", "hardware", "manifest", "models", "scripts", "student", "tests", "web",
    },
    "zephyr-virtual-board": {"docs", "scripts", "src", "tests", "zephyr"},
}
LEGAL_FILES = {"LICENSE", "LICENSE.txt", "LICENSE.md", "NOTICE", "NOTICE.txt",
               "NOTICE.md", "COPYING"}
ROOT_FILES = {
    "emul-parking-lot": {"README.md", ".gitignore", "requirements-tools.txt"},
    "zephyr-virtual-board": {
        "README.md", "CONTRIBUTING.md", ".gitignore", "pyproject.toml",
    },
}
EXTENSIONS = {
    ".c", ".h", ".cs", ".py", ".sh", ".cjs", ".js", ".html", ".css",
    ".md", ".txt", ".conf", ".overlay", ".repl", ".resc", ".robot",
    ".json", ".yaml", ".yml", ".toml",
}
SPECIAL_FILES = {"CMakeLists.txt", "Kconfig"}
EXCLUDED = {
    "build", "deps", "modules", ".git", ".west", ".venv", "venv",
    "__pycache__", "node_modules", ".pytest_cache", ".mypy_cache",
    ".cache", ".ruff_cache", ".tox", "dist",
    "secrets", "secret", "credentials", "private", "instructor", "instructor-local",
    ".ssh", ".aws",
}
STUDENT_DOCS = {
    "agent-context.md", "architecture.md", "browser.md", "device-protocols.md",
    "hardware-models.md", "physical-model.md", "release-checklist.md",
    "scenarios.md", "setup.md", "student-specification.md",
}
STUDENT_SCRIPTS = {
    "assess.py", "bounded-command.py", "check-student-launch.py", "lab.py",
    "package-lab.py", "parking-web.py", "parking_embedded.py", "parking_web.py",
    "prepare-smoke.py", "scenario-run.py", "scenario-schema.py",
    "scenario/__init__.py", "scenario/assessment.py", "scenario/observations.py",
    "scenario/obstruction.py", "scenario/outcome.py", "scenario/parser.py",
    "scenario/robot_library.py", "scenario/schema.py",
}
STUDENT_TESTS = {
    "scenario.robot", "test_bounded_command.py", "test_browser_history.cjs",
    "test_lab.py", "test_lab_launch_validation.py", "test_package_lab.py",
    "test_parking_observations.py", "test_parking_web.py", "test_scenario.py",
    "test_scenario_obstruction.py",
}


def student_source(relative):
    """Curate host infrastructure; never infer that every test/doc is a handout."""
    parts = relative.parts
    if parts[0] == "docs":
        return len(parts) == 2 and parts[1] in STUDENT_DOCS
    if parts[0] == "scripts":
        return Path(*parts[1:]).as_posix() in STUDENT_SCRIPTS
    if parts[0] == "tests":
        return ((len(parts) == 2 and parts[1] in STUDENT_TESTS)
                or (len(parts) == 3 and parts[1] == "scenarios"
                    and relative.suffix == ".json"))
    return True


def excluded(name):
    """Conservative name filter, not a claim to detect embedded credentials."""
    lower = name.lower()
    return (lower in EXCLUDED or lower.startswith(".env")
            or any(word in lower for word in
                   ("secret", "credential", "token", "password", "passwd",
                    "api_key", "apikey", "access_key"))
            or lower.endswith((".pem", ".key", ".p12", ".pfx")))


def source_files(root, name):
    """Walk only approved source trees, never following symlinks."""
    for entry in sorted(root.iterdir()):
        if entry.is_symlink():
            continue
        if entry.is_file() and entry.name in ROOT_FILES[name] | LEGAL_FILES:
            yield entry
        elif entry.is_dir() and entry.name in DIRECTORIES[name]:
            for directory, dirs, files in os.walk(entry, followlinks=False):
                base = Path(directory)
                dirs[:] = sorted(d for d in dirs if not excluded(d)
                                 and not (base / d).is_symlink())
                for filename in sorted(files):
                    candidate = base / filename
                    if (not excluded(filename) and not candidate.is_symlink()
                            and candidate.is_file()
                            and (name != "emul-parking-lot"
                                 or student_source(candidate.relative_to(root)))
                            and (candidate.suffix in EXTENSIONS
                                 or filename in SPECIAL_FILES | LEGAL_FILES)):
                        yield candidate


def git_base(root, *, student=False):
    """HEAD is only a base: archive hashes identify the actual packaged bytes."""
    def git(*args):
        return subprocess.run(
            ["git", "-C", str(root), *args], check=True, capture_output=True,
            text=True, timeout=5,
        ).stdout.strip()
    try:
        if Path(git("rev-parse", "--show-toplevel")).resolve() != root:
            return {"commit": None, "dirty": None, "status": "not_repository_root"}
        commit = git("rev-parse", "--verify", "HEAD")
        dirty = bool(git("--no-optional-locks", "status", "--porcelain"))
        if student and git("rev-list", "HEAD", "--", "reference", "probes", "app",
                           "tests/native", "docs/instructor-guide.md"):
            # An older solution commit is not a student artifact identity.
            # Per-file hashes still identify the curated bytes.
            return {"commit": None, "dirty": dirty, "status": "instructor_history_excluded"}
        return {"commit": commit, "dirty": dirty, "status": "available"}
    except (OSError, subprocess.SubprocessError):
        return {"commit": None, "dirty": None, "status": "unavailable_or_unborn"}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def output_directory(root, output):
    output = Path(output).expanduser()
    if not output.is_absolute():
        output = root / output
    # Reject lexical traversal and symlink ancestors before creating anything.
    if ".." in output.parts:
        raise ValueError("--output must not contain '..'")
    build = root / "build"
    if output == build or not output.is_relative_to(build):
        raise ValueError("--output must be a new directory beneath build/")
    for parent in (output, *output.parents):
        if parent == root:
            break
        if parent.is_symlink():
            raise ValueError("--output must not traverse symlinks")
    if output.exists():
        raise ValueError("--output must be a new directory")
    return output


def package(module, output, root=ROOT):
    root = Path(root).resolve(strict=True)
    module = Path(module).expanduser().absolute()
    if module.is_symlink():
        raise ValueError("--module must not be a symlink")
    module = module.resolve(strict=True)
    output = output_directory(root, output)
    sources = {"emul-parking-lot": root, "zephyr-virtual-board": module}
    selected = {name: list(source_files(base, name))
                for name, base in sources.items()}
    module_paths = {p.relative_to(module).as_posix()
                    for p in selected["zephyr-virtual-board"]}
    if not {"src/zvb/_resc.py", "scripts/west-commands.yml"} <= module_paths:
        raise ValueError("--module must name a non-symlink zephyr-virtual-board source tree")
    manifest = {
        "version": 1,
        "purpose": "Student source candidate; local build only, not a published release",
        "audience": "student",
        "release_blockers": [],
        "limitations": [
            "Allowlist and filename exclusions do not detect secrets embedded in source.",
            "Path selection cannot detect a solution copied into an allowed source file.",
            "Git commits identify source bases only; per-file hashes identify packaged bytes.",
            "Student Git base is withheld when its history contains instructor source.",
        ],
        "selection": {
            "root_files": {name: sorted(files | LEGAL_FILES)
                           for name, files in ROOT_FILES.items()},
            "source_directories": {name: sorted(dirs)
                                   for name, dirs in DIRECTORIES.items()},
            "extensions": sorted(EXTENSIONS),
            "special_files": sorted(SPECIAL_FILES | LEGAL_FILES),
            "excluded_names": sorted(EXCLUDED),
            "student_docs": sorted(STUDENT_DOCS),
            "student_scripts": sorted(STUDENT_SCRIPTS),
            "student_tests": sorted(STUDENT_TESTS),
            "public_scenarios": "tests/scenarios/*.json",
            "instructor_directories_excluded": [
                "app", "instructor", "instructor-local", "probes", "reference",
            ],
            "symlinks": "excluded",
        },
        "sources": {},
    }
    for name, base in sources.items():
        licenses = [p.name for p in selected[name]
                    if p.parent == base and p.name in LEGAL_FILES
                    and not p.name.startswith("NOTICE")]
        if not licenses:
            manifest["release_blockers"].append(
                f"{name}: root license missing; no redistribution grant inferred"
            )
        manifest["sources"][name] = {
            "source_base_git": git_base(base, student=name == "emul-parking-lot"),
            "root_license_files": licenses,
        }
    manifest["distribution_status"] = (
        "License-pending local review only; not a redistributable release"
        if manifest["release_blockers"] else
        "Local review candidate; no publication authorization")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.mkdir()  # Exclusive creation; never overwrite a prior package.
    for name, base in sources.items():
        archive = output / f"{name}.zip"
        files = []
        with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as bundle:
            for source in selected[name]:
                relative = source.relative_to(base).as_posix()
                data = source.read_bytes()
                archive_path = f"{name}/{relative}"
                # Stable timestamps and explicit regular-file modes, no links.
                info = zipfile.ZipInfo(archive_path, date_time=(1980, 1, 1, 0, 0, 0))
                info.create_system = 3
                info.external_attr = (0o100755 if source.stat().st_mode & 0o111
                                      else 0o100644) << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                bundle.writestr(info, data)
                files.append({"path": relative, "archive_path": archive_path,
                              "size": len(data), "sha256": digest(data)})
        manifest["sources"][name].update({
            "archive": archive.name, "archive_sha256": digest(archive.read_bytes()),
            "files": files,
        })
    (output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        manifest = package(args.module, args.output)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f"Created two local source archives and manifest in {args.output}")
    for blocker in manifest["release_blockers"]:
        print(f"RELEASE BLOCKER: {blocker}")


if __name__ == "__main__":
    main()
