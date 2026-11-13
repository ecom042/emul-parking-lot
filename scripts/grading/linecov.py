"""Line coverage of student sources from Renode block lists and DWARF.

`models/BlockCoverage.cs` records each distinct translation block as
"<hex start address> <instruction count>". Thumb-2 instruction lengths come
from the ELF bytes, so every executed instruction address is known exactly.
A source line is *executable* when the DWARF line table gives it at least one
address range, and *hit* when an executed instruction starts inside one.
Code the linker discarded has no addresses and is therefore not counted.
"""
import bisect
import os
from pathlib import Path
import re

from elftools.elf.constants import SH_FLAGS
from elftools.elf.elffile import ELFFile

SOURCE_SUFFIXES = {".c", ".h"}
# A reason is mandatory, for example: /* LCOV_EXCL_LINE: unreachable default */
MARKER = re.compile(r"LCOV_EXCL_(LINE|START|STOP)\b(?:\s*:\s*((?:(?!\*/).)*\S))?")
EXCLUSION_CAP = 0.03


def thumb_length(halfword):
    """Thumb-2: a first halfword of 0b11101/0b11110/0b11111 starts a 32-bit one."""
    return 4 if (halfword >> 11) in (0b11101, 0b11110, 0b11111) else 2


def read_blocks(paths):
    blocks = set()
    for path in paths:
        for line in Path(path).read_text().splitlines():
            if line.strip():
                address, count = line.split()
                blocks.add((int(address, 16), int(count)))
    return blocks


def executable_sections(elf):
    sections = []
    for section in elf.iter_sections():
        if section["sh_flags"] & SH_FLAGS.SHF_EXECINSTR and section["sh_type"] == "SHT_PROGBITS":
            sections.append((section["sh_addr"], section.data()))
    return sections


def instruction_addresses(elf_path, blocks):
    """Start address of every instruction inside the executed blocks."""
    with open(elf_path, "rb") as stream:
        sections = executable_sections(ELFFile(stream))
    addresses = set()
    for start, count in blocks:
        for base, data in sections:
            if base <= start < base + len(data):
                address = start
                for _ in range(count):
                    offset = address - base
                    if offset + 2 > len(data):
                        break
                    addresses.add(address)
                    address += thumb_length(int.from_bytes(data[offset:offset + 2], "little"))
                break
    return addresses


def _file_path(program, cu, index):
    version = program.header.version
    entries = program["file_entry"]
    entry = entries[index] if version >= 5 else entries[index - 1]
    name = entry.name.decode()
    directories = program["include_directory"]
    top = cu.get_top_DIE().attributes
    comp_dir = top["DW_AT_comp_dir"].value.decode() if "DW_AT_comp_dir" in top else ""
    if version >= 5:
        directory = directories[entry.dir_index].decode()
    else:
        directory = directories[entry.dir_index - 1].decode() if entry.dir_index else comp_dir
    path = Path(name) if os.path.isabs(name) else Path(directory) / name
    if not path.is_absolute():
        path = Path(comp_dir) / path
    return Path(os.path.normpath(path))


def line_ranges(elf_path, app):
    """{(relative source, line): [(low, high), ...]} for files under app/src."""
    source_root = Path(os.path.realpath(Path(app) / "src"))
    ranges = {}
    with open(elf_path, "rb") as stream:
        dwarf = ELFFile(stream).get_dwarf_info()
        for cu in dwarf.iter_CUs():
            program = dwarf.line_program_for_CU(cu)
            if program is None:
                continue
            names = {}
            previous = None
            for entry in program.get_entries():
                state = entry.state
                if state is None:
                    continue
                if previous is not None and state.address > previous.address:
                    if previous.file not in names:
                        path = Path(os.path.realpath(_file_path(program, cu, previous.file)))
                        names[previous.file] = (path.relative_to(source_root.parent).as_posix()
                                                if path.is_relative_to(source_root)
                                                and path.suffix in SOURCE_SUFFIXES else None)
                    name = names[previous.file]
                    if name is not None:
                        ranges.setdefault((name, previous.line), []).append(
                            (previous.address, state.address))
                previous = None if state.end_sequence else state
    return ranges


def hit_lines(ranges, addresses):
    """Lines with at least one executed instruction start in their ranges."""
    ordered = sorted(addresses)
    hits = set()
    for key, spans in ranges.items():
        for low, high in spans:
            index = bisect.bisect_left(ordered, low)
            if index < len(ordered) and ordered[index] < high:
                hits.add(key)
                break
    return hits


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


def summarize(ranges, scenario_hits, excluded, problems=(), cap=EXCLUSION_CAP):
    """Merge per-scenario hits into totals, per-file figures and a line map."""
    executable = set(ranges)
    applied = excluded & executable
    notes = list(problems)
    if executable and len(applied) > cap * len(executable):
        notes.append(f"{len(applied)} excluded lines exceed the {cap:.0%} cap; "
                     "no exclusion was applied")
        applied = set()
    if problems:
        notes.append("exclusion markers without a reason; no exclusion was applied")
        applied = set()
    counted = executable - applied
    covered = set().union(*scenario_hits.values()) & counted if scenario_hits else set()
    files = {}
    for name, line in sorted(counted):
        entry = files.setdefault(name, {"executable": 0, "hit": 0, "uncovered": []})
        entry["executable"] += 1
        if (name, line) in covered:
            entry["hit"] += 1
        else:
            entry["uncovered"].append(line)
    for entry in files.values():
        entry["percent"] = round(100 * entry["hit"] / entry["executable"], 2)
    line_map = {}
    for scenario, hits in sorted(scenario_hits.items()):
        for name, line in sorted(hits & counted):
            line_map.setdefault(name, {}).setdefault(str(line), []).append(scenario)
    return {"executable": len(counted), "hit": len(covered),
            "percent": round(100 * len(covered) / len(counted), 2) if counted else 0.0,
            "excluded": len(applied), "notes": notes, "files": files}, line_map


def write_lcov(path, app, summary, line_map):
    """LCOV for editors and genhtml; counts are scenarios that hit the line."""
    lines = ["TN:firmware"]
    for name, entry in sorted(summary["files"].items()):
        lines.append(f"SF:{(Path(app) / name).resolve()}")
        uncovered = set(entry["uncovered"])
        hit = line_map.get(name, {})
        numbers = sorted({int(n) for n in hit} | uncovered)
        for number in numbers:
            lines.append(f"DA:{number},{0 if number in uncovered else len(hit[str(number)])}")
        lines += [f"LH:{entry['hit']}", f"LF:{entry['executable']}", "end_of_record"]
    Path(path).write_text("\n".join(lines) + "\n")
