#!/usr/bin/env python3
"""Static check of the student firmware with the SDK's own GCC.

Builds the application with Zephyr's GCC static analyzer variant
(-fanalyzer, ZEPHYR_SCA_VARIANT=gcc) and a fixed set of extra warnings, then
counts the diagnostics whose location is a file under <app>/src. Zephyr's own
sources and headers are analysed too but never counted. Nothing extra is
installed: the analyzer ships with the pinned Zephyr SDK.
"""
import argparse
import json
import os
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from grading import workspace  # noqa: E402

WARNINGS = ["-Wall", "-Wextra", "-Wshadow", "-Wundef", "-Wdouble-promotion", "-Wformat=2",
            "-Wcast-qual", "-Wstrict-prototypes", "-Wmissing-prototypes", "-Wnull-dereference",
            "-Wswitch-enum"]
SCA_CMAKE = ["-DZEPHYR_SCA_VARIANT=gcc", "-DEXTRA_CFLAGS=" + " ".join(WARNINGS)]
DIAGNOSTIC = re.compile(r"^(?P<path>[^:\n]+):(?P<line>\d+):(?P<column>\d+): "
                        r"(?P<severity>warning|error): (?P<message>.*?)(?: \[(?P<flag>[^\]]+)\])?$")


def findings(log_text, app):
    """Unique diagnostics located in <app>/src, in log order."""
    source_root = Path(os.path.realpath(Path(app) / "src"))
    seen, found = set(), []
    for line in log_text.splitlines():
        match = DIAGNOSTIC.match(line.strip())
        if not match:
            continue
        path = Path(os.path.realpath(match["path"]))
        if not path.is_relative_to(source_root):
            continue
        name = path.relative_to(source_root.parent).as_posix()
        key = (name, int(match["line"]), match["flag"] or match["message"])
        if key in seen:
            continue
        seen.add(key)
        found.append({"file": name, "line": int(match["line"]), "column": int(match["column"]),
                      "severity": match["severity"], "flag": match["flag"],
                      "message": match["message"]})
    return found


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--module", type=Path, required=True)
    ap.add_argument("--renode", type=Path, required=True,
                    help="unused by the check; accepted for a uniform command line")
    ap.add_argument("--output", type=Path, required=True, help="new directory under build/")
    ap.add_argument("--app", type=Path, default=workspace.ROOT / "student")
    ap.add_argument("--cmake-arg", action="append", default=[])
    args = ap.parse_args(argv)
    output = workspace.new_output(args.output)
    app = args.app.resolve()
    workspace.select_environment(args.module)
    _, error = workspace.build_firmware(args.module, args.renode, output, app,
                                        SCA_CMAKE + args.cmake_arg)
    log = (output / "build.log").read_text(errors="replace")
    found = findings(log, app)
    summary = {"v": 1, "app": str(app), "build_ok": error is None, "warnings": WARNINGS,
               "count": len(found), "findings": found}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    for item in found:
        flag = f" [{item['flag']}]" if item["flag"] else ""
        print(f"{item['file']}:{item['line']}:{item['column']}: {item['severity']}: "
              f"{item['message']}{flag}")
    if error:
        print(error)
    print(f"{len(found)} finding(s) in {app / 'src'}; details: {output / 'summary.json'}")
    return 0 if error is None and not found else 1


if __name__ == "__main__":
    raise SystemExit(main())
