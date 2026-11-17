// Page layout for the PDF copies of the student documents.
// scripts/docs-pdf.py compiles this file once per document, passing
//   --input source=<repository path of the Markdown file>
//   --input markdown=<root-relative paths of the rewritten Markdown, joined by ;>
//   --input lang=<pt|en>
#import "@preview/cmarker:0.1.6": render

#let source = sys.inputs.at("source")
#let lang = sys.inputs.at("lang", default: "pt")
#let generated = if lang == "pt" {
  [Gerado de #raw(source). Em caso de divergência, vale o Markdown.]
} else {
  [Generated from #raw(source). The Markdown file is authoritative.]
}

#set document(title: source)
#set text(lang: lang, size: 10pt, font: "Libertinus Serif")
#set par(justify: true, leading: 0.6em)
#set page(
  paper: "a4",
  margin: (x: 2cm, top: 2.2cm, bottom: 2.4cm),
  header: context if counter(page).get().first() > 1 [
    #set text(size: 8pt, fill: luma(110))
    emul-parking-lot #h(1fr) #raw(source)
  ],
  footer: context [
    #set text(size: 8pt, fill: luma(110))
    #generated #h(1fr) #counter(page).display("1 / 1", both: true)
  ],
)

#show heading: set text(fill: rgb("#1f3f6e"))
#show quote.where(block: true): it => block(
  width: 100%, fill: rgb("#eef4fb"), stroke: (left: 3pt + rgb("#1f5fa8")),
  inset: (x: 10pt, y: 8pt), radius: 2pt, it.body,
)
#show heading.where(level: 1): it => block(below: 1em)[#text(size: 18pt, it)]
#show heading.where(level: 2): it => block(above: 1.4em, below: 0.8em)[#text(size: 14pt, it)]
#show heading.where(level: 3): set text(size: 11.5pt)
#show link: set text(fill: rgb("#1f5fa8"))
#show raw: set text(font: "DejaVu Sans Mono", size: 8.5pt)
#show raw.where(block: true): it => block(
  width: 100%, fill: luma(245), inset: 8pt, radius: 3pt, stroke: luma(220), it,
)
#show raw.where(block: false): box.with(fill: luma(240), inset: (x: 2pt), outset: (y: 2pt), radius: 2pt)
#set table(
  stroke: (x, y) => (bottom: 0.5pt + luma(190), top: if y == 0 { 0.8pt + luma(120) }),
  inset: 5pt,
  fill: (x, y) => if y == 0 { luma(235) },
)
#show table: set text(size: 8.8pt)
#show table: set par(justify: false)
#show table.cell.where(y: 0): set text(weight: "bold")
#set list(indent: 0.6em)
#set enum(indent: 0.6em)

#for (i, markdown) in sys.inputs.at("markdown").split(";").enumerate() {
  if i > 0 { pagebreak() }
  render(
    read(markdown),
    scope: (image: (path, alt: none, format: auto) => image(path, alt: alt, format: format, width: 100%)),
  )
}
