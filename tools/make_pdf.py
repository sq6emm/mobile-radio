#!/usr/bin/env python3
"""Build the review PDF from README.md, docs/*.md, the BOM and the schematic sheets.

    python3 tools/make_pdf.py [--out docs/mobile-radio-design.pdf]

Pipeline: Markdown (GFM) -> pandoc -> one HTML file with a cover page and a
table of contents -> Chromium (Playwright, tools/render_pdf.js) -> PDF.
The two mermaid diagrams are replaced by hand-drawn SVGs (tools/diagrams.py)
so no browser-side library or network access is needed.
"""
import argparse
import csv
import datetime
import html
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(ROOT, "docs")

CSS = """
@page { size: A4; }
body { font-family: "DejaVu Sans", "Liberation Sans", Arial, sans-serif; font-size: 10.5pt; line-height: 1.4; color: #111; }
h1 { font-size: 20pt; border-bottom: 2px solid #333; padding-bottom: 4px; margin-top: 0; page-break-before: always; }
h1.first { page-break-before: avoid; }
h2 { font-size: 14pt; margin-top: 1.4em; color: #222; }
h3 { font-size: 11.5pt; margin-top: 1.1em; }
table { border-collapse: collapse; width: 100%; margin: 0.6em 0 1em 0; font-size: 9pt; page-break-inside: auto; }
th, td { border: 1px solid #999; padding: 3px 5px; vertical-align: top; text-align: left; }
th { background: #e8e8e8; }
tr { page-break-inside: avoid; }
code { font-family: "DejaVu Sans Mono", "Liberation Mono", monospace; font-size: 8.8pt; background: #f3f3f3; padding: 0 2px; }
pre { background: #f5f5f5; border: 1px solid #ddd; padding: 6px 8px; font-size: 8.3pt; white-space: pre-wrap; word-break: break-word; page-break-inside: avoid; }
pre code { background: none; padding: 0; }
.diagram { text-align: center; margin: 0.6em 0 1em 0; page-break-inside: avoid; }
.diagram svg { max-width: 100%; height: auto; }
blockquote { border-left: 3px solid #bbb; margin-left: 0; padding-left: 10px; color: #444; }
.cover { text-align: left; padding-top: 60mm; page-break-after: always; break-after: page; }
.cover .title { font-size: 30pt; font-weight: bold; line-height: 1.15; margin-bottom: 0.3em; }
.cover .sub { font-size: 14pt; color: #333; margin: 0.4em 0 2em 0; }
.cover table { width: auto; font-size: 10pt; }
.cover td { border: none; padding: 2px 14px 2px 0; }
#TOC { page-break-after: always; }
#TOC ul { list-style: none; padding-left: 1.2em; }
#TOC > ul { padding-left: 0; }
#TOC a { text-decoration: none; color: #111; }
.note { font-size: 9pt; color: #444; }
a { color: #0645ad; }
img { max-width: 100%; }
"""

def replace_mermaid(body):
    """Swap each mermaid block for the hand-drawn SVG of the same diagram (tools/diagrams.py)."""
    import diagrams  # noqa: WPS433

    def sub(m):
        src = html.unescape(m.group(1))
        for key, fn in diagrams.DIAGRAMS.items():
            if src.lstrip().startswith(key):
                return '<div class="diagram">' + fn() + '</div>'
        return '<pre class="mermaid">' + m.group(1) + '</pre>'
    return re.sub(r'<pre class="mermaid"><code>(.*?)</code></pre>', sub, body, flags=re.S)


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kw).stdout


def git(*args):
    try:
        return run(["git", "-C", ROOT, *args]).strip()
    except Exception:
        return "n/a"


def bom_markdown():
    with open(os.path.join(ROOT, "hardware", "bom.csv"), newline="") as fh:
        rows = list(csv.reader(fh))
    head, body = rows[0], rows[1:]
    out = ["# Appendix A. Bill of materials", "", "Source: `hardware/bom.csv`.", ""]
    out.append("| " + " | ".join(head) + " |")
    out.append("|" + "---|" * len(head))
    for r in body:
        out.append("| " + " | ".join(c.replace("|", "/") for c in r) + " |")
    return "\n".join(out) + "\n"


def schematic_markdown():
    d = os.path.join(ROOT, "hardware", "schematics")
    parts = ["# Appendix B. Schematic connection tables", "", "Source: `hardware/schematics/`.", ""]
    for name in sorted(os.listdir(d)):
        if not name.startswith("sheet") or not name.endswith(".md"):
            continue
        with open(os.path.join(d, name)) as fh:
            text = fh.read()
        text = re.sub(r"^# ", "## ", text, count=1, flags=re.M)      # demote sheet titles
        text = re.sub(r"^## (?!Sheet)", "### ", text, flags=re.M)
        parts.append(text)
    return "\n\n".join(parts) + "\n"


def results_markdown():
    out = ["# Appendix C. Generated RF design tables", "", "Output of `hardware/rf/filter_design.py` and `antenna_calc.py`.", ""]
    for name in ("filter_summary.md", "antenna_report.txt"):
        path = os.path.join(ROOT, "hardware", "rf", "results", name)
        if not os.path.exists(path):
            continue
        with open(path) as fh:
            text = fh.read()
        if name.endswith(".txt"):
            text = "## Antenna report\n\n```\n" + text + "\n```\n"
        else:
            text = re.sub(r"^# .*$", "## Filter summary", text, count=1, flags=re.M)
        out.append(text)
    return "\n\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default=os.path.join(DOCS, "mobile-radio-design.pdf"))
    ap.add_argument("--keep-html", action="store_true")
    a = ap.parse_args()

    build = os.path.join(ROOT, "docs", "build")
    os.makedirs(build, exist_ok=True)

    chapters = [os.path.join(ROOT, "README.md")]
    chapters += sorted(os.path.join(DOCS, f) for f in os.listdir(DOCS) if re.match(r"\d\d-.*\.md$", f))
    md_parts = []
    for i, path in enumerate(chapters):
        with open(path) as fh:
            text = fh.read()
        if i == 0:
            text = text.replace("# mobile-radio", "# 0. Overview", 1)
        md_parts.append(text)
    md_parts += [bom_markdown(), schematic_markdown(), results_markdown()]
    md = "\n\n".join(md_parts)
    md_path = os.path.join(build, "all.md")
    with open(md_path, "w") as fh:
        fh.write(md)

    body = run(["pandoc", "-f", "gfm", "-t", "html5", "--toc", "--toc-depth=2", md_path])
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    body = replace_mermaid(body)
    # split TOC from content (pandoc puts TOC first when --toc with a template-less fragment)
    date = datetime.date.today().isoformat()
    commit, branch = git("rev-parse", "--short", "HEAD"), git("rev-parse", "--abbrev-ref", "HEAD")
    title = "mobile-radio: USB-C battery pack with UHF radio"
    cover = f"""
<div class="cover">
  <div class="title">{html.escape(title)}</div>
  <div class="sub">Design document for review: PMR446 + 70 cm FM, LoRa 433/868, APRS and messaging,
  FreeDV digital voice, USB-C Power Delivery, sound-card profile, ESP32-C5 option</div>
  <table>
    <tr><td>Status</td><td>review draft, comments welcome on any numbered chapter / table / requirement ID</td></tr>
    <tr><td>Date</td><td>{date}</td></tr>
    <tr><td>Repository</td><td>github.com/sq6emm/mobile-radio, branch {html.escape(branch)}, commit {html.escape(commit)}</td></tr>
    <tr><td>Built with</td><td>tools/make_pdf.py (pandoc + Chromium)</td></tr>
  </table>
  <p class="note">Items marked <b>VERIFY</b> must be confirmed on hardware. Chapter 15 is a trade study that asks
  for a decision; chapters 1-14 reflect decisions already taken in the design reviews.</p>
</div>
"""
    doc = f"""<!doctype html><html><head><meta charset="utf-8"><title>{html.escape(title)}</title>
<style>{CSS}</style></head><body>{cover}{body}</body></html>"""
    # the first content h1 should not force a page break right after the TOC page
    doc = doc.replace('<h1 id="overview">', '<h1 class="first" id="overview">', 1)
    html_path = os.path.join(build, "mobile-radio-design.html")
    with open(html_path, "w") as fh:
        fh.write(doc)

    subprocess.run(["node", os.path.join(ROOT, "tools", "render_pdf.js"), html_path, a.out, title], check=True)
    if not a.keep_html:
        os.remove(md_path)
    print(f"wrote {a.out}")


if __name__ == "__main__":
    sys.exit(main())
