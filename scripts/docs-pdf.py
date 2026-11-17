#!/usr/bin/env python3
"""PDF copies of the documents students read, committed in docs/pdf/.

    python scripts/docs-pdf.py            # regenerate the PDFs that are stale
    python scripts/docs-pdf.py --all      # regenerate every PDF
    python scripts/docs-pdf.py --check    # exit 1 if a PDF is stale (no Typst needed)

The Markdown files stay authoritative. Each PDF is compiled with Typst and the
pinned @preview/cmarker package (downloaded by Typst on first use) through
scripts/docs-pdf.typ, one PDF per bundle (the brief and the installation
guide go to students as a single file). Links to a document of the set keep
only their text;
links to other repository files point to GitHub; links inside the same
document keep only their text. docs/pdf/SOURCES.json records a hash of the
inputs of each PDF, so --check (and tests/test_docs_pdf.py) catches a
document edited without regenerating its PDF. Typst output is not
byte-reproducible across versions, hence the input hashes.
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs/pdf"
MANIFEST = OUTPUT / "SOURCES.json"
TEMPLATE = ROOT / "scripts/docs-pdf.typ"
REPOSITORY = "https://github.com/ecom042/emul-parking-lot/blob/main/"

# Repository path -> language. Only the documents people read: the
# Portuguese challenge brief and installation guide. The English technical
# references and AGENTS.md are read by agents and stay Markdown only.
DOCUMENTS = {
    "docs/desafio.md": "pt",
    "docs/instalacao.md": "pt",
}
# PDF name -> the documents it holds, in order: students receive one file.
BUNDLES = {"desafio.pdf": ["docs/desafio.md", "docs/instalacao.md"]}

LINK = re.compile(r"(!?)\[((?:[^\[\]]|\[[^\]]*\])*)\]\(([^)\s]+)\)")


def rewrite(source, text):
    """Markdown with links and image paths valid inside docs/pdf/."""
    base = (ROOT / source).parent

    def link(match):
        bang, label, target = match.groups()
        if re.match(r"[a-z]+:", target):
            return match.group(0)
        path, _, anchor = target.partition("#")
        if not path:
            return label  # same-document anchor: Typst has no matching label
        relative = (base / path).resolve().relative_to(ROOT).as_posix()
        if bang:
            return f"![{label}](/{relative})"
        if relative in DOCUMENTS:
            return label  # in the same PDF; Typst has no matching label
        return f"[{label}]({REPOSITORY}{relative}{'#' + anchor if anchor else ''})"

    return LINK.sub(link, text)


def images(text):
    return sorted({m.group(3) for m in LINK.finditer(text) if m.group(1)})


def digest(name):
    """Hash of everything a PDF is made from."""
    h = hashlib.sha256()
    for part in [TEMPLATE.read_bytes(), Path(__file__).read_bytes()]:
        h.update(hashlib.sha256(part).digest())
    for source in BUNDLES[name]:
        text = (ROOT / source).read_text()
        h.update(hashlib.sha256(text.encode()).digest())
        for image in images(text):
            h.update(hashlib.sha256(((ROOT / source).parent / image).read_bytes()).digest())
    return h.hexdigest()


def stale(manifest):
    return [n for n in BUNDLES if manifest.get(n) != digest(n) or not (OUTPUT / n).is_file()]


def build(name, typst, work):
    sources = BUNDLES[name]
    markdowns = []
    for source in sources:
        markdown = work / (Path(source).stem + ".md")
        markdown.write_text(rewrite(source, (ROOT / source).read_text()))
        markdowns.append("/" + markdown.relative_to(ROOT).as_posix())
    command = [typst, "compile", "--root", str(ROOT),
               "--input", f"source={', '.join(sources)}",
               "--input", f"markdown={';'.join(markdowns)}",
               "--input", f"lang={DOCUMENTS[sources[0]]}",
               str(TEMPLATE), str(OUTPUT / name)]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"{name}: typst failed\n{result.stderr}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true", help="only report stale PDFs")
    ap.add_argument("--all", action="store_true", help="regenerate every PDF")
    args = ap.parse_args()
    manifest = json.loads(MANIFEST.read_text()) if MANIFEST.is_file() else {}
    todo = list(BUNDLES) if args.all else stale(manifest)
    if args.check:
        for name in todo:
            print(f"stale: docs/pdf/{name} (from {', '.join(BUNDLES[name])})")
        return 1 if todo else 0
    typst = shutil.which("typst")
    if not typst:
        raise SystemExit("typst not found: install it from https://typst.app")
    OUTPUT.mkdir(exist_ok=True)
    work = ROOT / "build/docs-pdf-src"
    work.mkdir(parents=True, exist_ok=True)
    try:
        for name in todo:
            build(name, typst, work)
            manifest[name] = digest(name)
            print(f"docs/pdf/{name}")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for stray in set(p.name for p in OUTPUT.glob("*.pdf")) - set(BUNDLES):
        (OUTPUT / stray).unlink()
    manifest = {n: manifest[n] for n in BUNDLES if n in manifest}
    MANIFEST.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
