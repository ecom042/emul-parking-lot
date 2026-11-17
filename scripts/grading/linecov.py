"""Exclusions, totals and LCOV for line and branch coverage of student sources.

The measurements come from gcov (grading/gcov.py): executable lines
`(file, line)` and branch outcomes `(file, line, index)` of `<app>/src`, and
the ones each passing scenario or suite ran. A reasoned LCOV_EXCL marker or an
announced pattern drops a line together with its branches.
"""
from pathlib import Path
import re

SOURCE_SUFFIXES = {".c", ".h"}
# A reason is mandatory, for example: /* LCOV_EXCL_LINE: unreachable default */
MARKER = re.compile(r"LCOV_EXCL_(LINE|START|STOP)\b(?:\s*:\s*((?:(?!\*/).)*\S))?")
EXCLUSION_CAP = 0.03


def exclusions(app):
    """{(relative source, line)} marked excluded, plus marker problems."""
    excluded, problems = set(), []
    source_root = Path(app) / "src"
    for path in sorted(source_root.rglob("*")):
        if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
            continue
        name = path.relative_to(source_root.parent).as_posix()
        start = None
        for number, text in enumerate(path.read_text(errors="replace").splitlines(), 1):
            for match in MARKER.finditer(text):
                kind, reason = match.group(1), match.group(2)
                if not reason and kind != "STOP":
                    problems.append(f"{name}:{number}: LCOV_EXCL_{kind} needs ': reason'")
                if kind == "LINE":
                    excluded.add((name, number))
                elif kind == "START":
                    start = number
                elif start is not None:
                    excluded.update((name, n) for n in range(start, number + 1))
                    start = None
            if start is not None:
                excluded.add((name, number))
        if start is not None:
            problems.append(f"{name}:{start}: LCOV_EXCL_START without LCOV_EXCL_STOP")
    return excluded, problems


def pattern_exclusions(app, patterns):
    """{(relative source, line)} whose whole text matches one of `patterns`
    (as gcovr's --exclude-*-by-pattern: e.g. logging or *_DEFINE macros)."""
    compiled = [re.compile(p) for p in patterns]
    excluded = set()
    if not compiled:
        return excluded
    source_root = Path(app) / "src"
    for path in sorted(source_root.rglob("*")):
        if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
            continue
        name = path.relative_to(source_root.parent).as_posix()
        for number, text in enumerate(path.read_text(errors="replace").splitlines(), 1):
            if any(c.fullmatch(text) for c in compiled):
                excluded.add((name, number))
    return excluded


def summarize(executable, scenario_hits, excluded, problems=(), cap=EXCLUSION_CAP,
              by_pattern=frozenset(), branches=frozenset(), branch_hits=None):
    """Merge per-scenario hits into totals, per-file figures and a line map.

    `excluded` are reasoned markers, capped; `by_pattern` are lines removed by
    an announced pattern, outside the cap. Branch outcomes on a dropped line
    are dropped with it."""
    executable_lines = set(executable) - set(by_pattern)
    patterned = len(set(executable) & set(by_pattern))
    applied = excluded & executable_lines
    notes = list(problems)
    if executable_lines and len(applied) > cap * len(executable_lines):
        notes.append(f"{len(applied)} excluded lines exceed the {cap:.0%} cap; "
                     "no exclusion was applied")
        applied = set()
    if problems:
        notes.append("exclusion markers without a reason; no exclusion was applied")
        applied = set()
    counted = executable_lines - applied
    covered = set().union(*scenario_hits.values()) & counted if scenario_hits else set()
    counted_branches = {key for key in branches if key[:2] in counted}
    branch_hits = branch_hits or {}
    covered_branches = (set().union(*branch_hits.values()) & counted_branches
                        if branch_hits else set())
    files = {}
    for name, line in sorted(counted):
        entry = files.setdefault(name, {"executable": 0, "hit": 0, "uncovered": [],
                                        "branches": 0, "branches_hit": 0,
                                        "uncovered_branches": []})
        entry["executable"] += 1
        if (name, line) in covered:
            entry["hit"] += 1
        else:
            entry["uncovered"].append(line)
    for key in sorted(counted_branches):
        entry = files[key[0]]
        entry["branches"] += 1
        if key in covered_branches:
            entry["branches_hit"] += 1
        else:
            entry["uncovered_branches"].append(f"{key[1]}:{key[2]}")
    for entry in files.values():
        entry["percent"] = round(100 * entry["hit"] / entry["executable"], 2)
        entry["branch_percent"] = (round(100 * entry["branches_hit"] / entry["branches"], 2)
                                   if entry["branches"] else 100.0)
    line_map = {}
    for scenario, hits in sorted(scenario_hits.items()):
        for name, line in sorted(hits & counted):
            line_map.setdefault(name, {}).setdefault(str(line), []).append(scenario)
    branch_map = {}
    for scenario, hits in sorted(branch_hits.items()):
        for name, line, index in sorted(hits & counted_branches):
            branch_map.setdefault(name, {}).setdefault(f"{line}:{index}", []).append(scenario)
    return {"executable": len(counted), "hit": len(covered),
            "percent": round(100 * len(covered) / len(counted), 2) if counted else 0.0,
            "branches": len(counted_branches), "branches_hit": len(covered_branches),
            "branch_percent": (round(100 * len(covered_branches) / len(counted_branches), 2)
                               if counted_branches else 100.0),
            "excluded": len(applied), "excluded_by_pattern": patterned,
            "notes": notes, "files": files}, line_map, branch_map


def write_lcov(path, app, summary, line_map, branch_map=None):
    """LCOV for editors and genhtml; counts are scenarios that hit the line or
    took the branch outcome."""
    branch_map = branch_map or {}
    lines = ["TN:firmware"]
    for name, entry in sorted(summary["files"].items()):
        lines.append(f"SF:{(Path(app) / name).resolve()}")
        uncovered = set(entry["uncovered"])
        hit = line_map.get(name, {})
        numbers = sorted({int(n) for n in hit} | uncovered)
        for number in numbers:
            lines.append(f"DA:{number},{0 if number in uncovered else len(hit[str(number)])}")
        taken = branch_map.get(name, {})
        outcomes = sorted({tuple(map(int, k.split(":"))) for k in taken}
                          | {tuple(map(int, k.split(":"))) for k in entry["uncovered_branches"]})
        for line, index in outcomes:
            runs = taken.get(f"{line}:{index}")
            lines.append(f"BRDA:{line},0,{index},{len(runs) if runs else 0}")
        lines += [f"BRH:{entry['branches_hit']}", f"BRF:{entry['branches']}",
                  f"LH:{entry['hit']}", f"LF:{entry['executable']}", "end_of_record"]
    Path(path).write_text("\n".join(lines) + "\n")
