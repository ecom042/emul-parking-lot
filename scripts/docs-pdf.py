#!/usr/bin/env python3
"""PDF copies of the documents students read, committed in docs/pdf/.

    python scripts/docs-pdf.py            # regenerate the PDFs that are stale
    python scripts/docs-pdf.py --all      # regenerate every PDF
    python scripts/docs-pdf.py --check    # exit 1 if a PDF is stale (no Typst needed)

The Markdown files stay authoritative. Each PDF is compiled with Typst and the
pinned @preview/cmarker package (downloaded by Typst on first use) through
scripts/docs-pdf.typ. Links to another document in the set point to its PDF;
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

LINK = re.compile(r"(!?)\[((?:[^\[\]]|\[[^\]]*\])*)\]\(([^)\s]+)\)")


def pdf_name(source):
    return Path(source).stem + ".pdf"


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
            return f"[{label}]({pdf_name(relative)})"
        return f"[{label}]({REPOSITORY}{relative}{'#' + anchor if anchor else ''})"

    return LINK.sub(link, text)


def images(text):
    return sorted({m.group(3) for m in LINK.finditer(text) if m.group(1)})


def digest(source):
    """Hash of everything a PDF is made from."""
    text = (ROOT / source).read_text()
    h = hashlib.sha256()
    for part in [text.encode(), TEMPLATE.read_bytes(), Path(__file__).read_bytes()]:
        h.update(hashlib.sha256(part).digest())
    for image in images(text):
        h.update(hashlib.sha256(((ROOT / source).parent / image).read_bytes()).digest())
    return h.hexdigest()


def stale(manifest):
    return [s for s in DOCUMENTS
            if manifest.get(pdf_name(s)) != digest(s) or not (OUTPUT / pdf_name(s)).is_file()]


def build(source, typst, work):
    markdown = work / (Path(source).stem + ".md")
    markdown.write_text(rewrite(source, (ROOT / source).read_text()))
    command = [typst, "compile", "--root", str(ROOT),
               "--input", f"source={source}",
               "--input", f"markdown=/{markdown.relative_to(ROOT).as_posix()}",
               "--input", f"lang={DOCUMENTS[source]}",
               str(TEMPLATE), str(OUTPUT / pdf_name(source))]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"{source}: typst failed\n{result.stderr}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true", help="only report stale PDFs")
    ap.add_argument("--all", action="store_true", help="regenerate every PDF")
    args = ap.parse_args()
    manifest = json.loads(MANIFEST.read_text()) if MANIFEST.is_file() else {}
    todo = list(DOCUMENTS) if args.all else stale(manifest)
    if args.check:
        for source in todo:
            print(f"stale: docs/pdf/{pdf_name(source)} (from {source})")
        return 1 if todo else 0
    typst = shutil.which("typst")
    if not typst:
        raise SystemExit("typst not found: install it from https://typst.app")
    OUTPUT.mkdir(exist_ok=True)
    work = ROOT / "build/docs-pdf-src"
    work.mkdir(parents=True, exist_ok=True)
    try:
        for source in todo:
            build(source, typst, work)
            manifest[pdf_name(source)] = digest(source)
            print(f"docs/pdf/{pdf_name(source)}")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    manifest = {pdf_name(s): manifest[pdf_name(s)] for s in DOCUMENTS if pdf_name(s) in manifest}
    MANIFEST.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
