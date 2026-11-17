"""The committed PDFs in docs/pdf/ match their Markdown sources."""
import importlib.util
from pathlib import Path
import json
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/docs-pdf.py"
SPEC = importlib.util.spec_from_file_location("docs_pdf", SCRIPT)
docs_pdf = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(docs_pdf)


class CommittedPdfs(unittest.TestCase):
    def test_every_pdf_is_current(self):
        manifest = json.loads(docs_pdf.MANIFEST.read_text())
        self.assertEqual(docs_pdf.stale(manifest), [],
                         "run: python scripts/docs-pdf.py")

    def test_no_instructor_page_is_published(self):
        for source in ("docs/agent-context.md",
                       "docs/release-checklist.md", "AGENTS.md", "docs/setup.md"):
            self.assertNotIn(source, docs_pdf.DOCUMENTS)

    def test_students_receive_one_pdf(self):
        self.assertEqual(docs_pdf.BUNDLES,
                         {"desafio.pdf": ["docs/desafio.md", "docs/instalacao.md"]})
        self.assertEqual(sorted(p.name for p in docs_pdf.OUTPUT.glob("*.pdf")), ["desafio.pdf"])


class Links(unittest.TestCase):
    def test_links_are_valid_inside_docs_pdf(self):
        text = ("[a](setup.md#x) [b](instalacao.md#passo) [c](#local) "
                "[d](https://example.org/x.md) [e](../AGENTS.md) ![f](img/estacionamento.svg)")
        self.assertEqual(docs_pdf.rewrite("docs/desafio.md", text),
                         f"[a]({docs_pdf.REPOSITORY}docs/setup.md#x) "
                         "b c "
                         f"[d](https://example.org/x.md) [e]({docs_pdf.REPOSITORY}AGENTS.md) "
                         "![f](/docs/img/estacionamento.svg)")


if __name__ == "__main__":
    unittest.main()
