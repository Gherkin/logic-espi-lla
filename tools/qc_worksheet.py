#!/usr/bin/env python3
"""Generate a human QC checklist from the specification-derived tables.

The tables in src/core/tables/ are transcription work, and a transcription
checked only by its author is checked once. This turns them into something a
person can verify against a rendered spec page without reading any C++.

    python3 tools/qc_worksheet.py                 # markdown to stdout
    python3 tools/qc_worksheet.py --status        # just the pending summary
    python3 tools/qc_worksheet.py -o qc.md

Generating the worksheet from the tables is safe precisely because the
worksheet is not the oracle -- the specification is, and the reader is. What
this cannot do is tell you whether a value is right; it tells you exactly
which values to check and where to look them up.

Signing off: change ESPI_HUMAN_PENDING("QC-n") to ESPI_HUMAN_VERIFIED("date")
in the table header. --status then stops listing it.
"""

import argparse
import re
import sys
from pathlib import Path

# Provenance banners are not confined to the private tables directory: the CRC
# parameters (QC-1) and the lane assignment (QC-4) are public API, but they are
# still transcribed facts and still need a human to read them against a page.
SCAN_DIRS = [Path("src/core/tables"), Path("src/core/include/espi")]
EXCLUDE = {"TableProvenance.h"}

# Provenance fields live in the banner comment above each table.
FIELD_RE = {
    "source": re.compile(r"^//\s+SOURCE\s+(.*?)\s*$"),
    "crossref": re.compile(r"^//\s+CROSSREF\s+(.*?)\s*$"),
    "rendered": re.compile(r"^//\s+RENDERED\s+(.*?)\s*$"),
    "human": re.compile(r"^//\s+HUMAN\s+(.*?)\s*$"),
}
# The banner title is the first comment line after the opening divider.
DIVIDER_RE = re.compile(r"^//\s*-{10,}\s*$")
TITLE_RE = re.compile(r"^//\s{2}(\S.*?)\s*$")
TABLE_DEF_RE = re.compile(r"^#define\s+(ESPI_\w*TABLE)\s*\(\s*X\s*\)")
# Entries may carry a trailing block comment, e.g. `X( 0x2, 0 ) /* Reserved */`.
# Capturing it matters: dropping the row would hide the Reserved encoding from
# the very checklist meant to catch a wrong Reserved encoding.
ENTRY_RE = re.compile(r"^\s*X\(\s*(.*?)\s*\)\s*(?:/\*\s*(.*?)\s*\*/)?\s*\\?\s*$")
GROUP_RE = re.compile(r"^\s*/\*\s*-*\s*(.*?)\s*-*\s*\*/\s*\\?\s*$")
# Continuation of a provenance field: comment line indented past the keyword
# column and not itself a keyword.
CONTINUATION_RE = re.compile(r"^//\s{9,}(\S.*?)\s*$")


def parse_header(path: Path) -> dict:
    lines = path.read_text(encoding="utf-8").splitlines()
    banner = {"title": None, "source": "", "crossref": "", "rendered": "", "human": ""}
    tables, current, group, last_field = [], None, None, None
    after_divider = False

    for line in lines:
        matched_field = False
        for key, pattern in FIELD_RE.items():
            m = pattern.match(line)
            if m:
                # Keep the FIRST occurrence. A file may mention SOURCE again in
                # a subordinate comment -- IoMode.h cites section 3.10 for the
                # TAR constant -- and that must not displace the banner.
                if not banner[key]:
                    banner[key] = m.group(1)
                    last_field = key
                matched_field = True
                break

        if not matched_field and last_field and current is None:
            m = CONTINUATION_RE.match(line)
            if m:
                banner[last_field] = f"{banner[last_field]} {m.group(1)}".strip()
            elif line.startswith("//"):
                last_field = None

        if banner["title"] is None and not matched_field:
            if DIVIDER_RE.match(line):
                after_divider = True
            elif after_divider:
                m = TITLE_RE.match(line)
                if m:
                    banner["title"] = m.group(1).strip().rstrip(".")
                after_divider = False

        m = TABLE_DEF_RE.match(line)
        if m:
            current = {"macro": m.group(1), "entries": []}
            tables.append(current)
            group = None
            last_field = None
            continue

        if current is not None:
            m = GROUP_RE.match(line)
            if m and m.group(1):
                group = m.group(1)
                continue
            m = ENTRY_RE.match(line)
            if m:
                args = [a.strip() for a in m.group(1).split(",")]
                current["entries"].append((group, args, m.group(2) or ""))
                continue
            if line.strip() == "" or not line.rstrip().endswith("\\"):
                if current["entries"]:
                    current = None

    banner["file"] = path.name
    banner["path"] = str(path).replace("\\", "/")
    banner["pending"] = "PENDING" in banner["human"]
    banner["gate"] = ""
    gm = re.search(r"(QC-\d+)", banner["human"])
    if gm:
        banner["gate"] = gm.group(1)
    return {"banner": banner, "tables": tables}


def render(parsed_files: list) -> str:
    out = ["# eSPI mapping table QC worksheet", ""]
    out.append(
        "Each row is a value transcribed from the specification. Check it against "
        "the **rendered page**, not extracted text. Tick the row or correct the table."
    )
    out.append("")

    pending = [p for p in parsed_files if p["banner"]["pending"]]
    if pending:
        out.append("## Outstanding")
        out.append("")
        out.append("| Table | Gate | Source |")
        out.append("| --- | --- | --- |")
        for p in pending:
            b = p["banner"]
            out.append(f"| {b['title'] or b['file']} | {b['gate'] or '-'} | {b['source']} |")
        out.append("")

    for p in parsed_files:
        b = p["banner"]
        status = f"PENDING ({b['gate']})" if b["pending"] else b["human"]
        out.append(f"## {b['title'] or b['file']}")
        out.append("")
        out.append(f"- **Source** — {b['source']}")
        if b["crossref"] and "none" not in b["crossref"].lower():
            out.append(f"- **Cross-reference** — {b['crossref']}")
        out.append(f"- **Rendered check** — {b['rendered']}")
        out.append(f"- **Human sign-off** — {status}")
        out.append(f"- **Defined in** — `{b['path']}`")
        out.append("")

        if not p["tables"]:
            out.append(
                "No lookup table — the transcribed facts are stated in the header comment. "
                "Read that block against the source page."
            )
            out.append("")

        for table in p["tables"]:
            if not table["entries"]:
                continue
            width = min(max(len(a) for _, args, _ in table["entries"] for a in args), 24)
            last_group = None
            out.append(f"### `{table['macro']}`")
            out.append("")
            for grp, args, note in table["entries"]:
                if grp and grp != last_group:
                    if last_group is not None:
                        out.append("")
                    out.append(f"**{grp}**")
                    out.append("")
                    last_group = grp
                cells = "  ".join(a.ljust(width) for a in args).rstrip()
                suffix = f"  — {note}" if note else ""
                out.append(f"- [ ] `{cells}`{suffix}")
            out.append("")
    return "\n".join(out) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-o", "--output", help="write markdown here instead of stdout")
    ap.add_argument("--status", action="store_true", help="print pending tables only, and exit nonzero if any")
    args = ap.parse_args()

    # TableProvenance.h defines the bookkeeping machinery, not a fact. Its
    # explanatory comment uses the same field names, so exclude it by name
    # rather than by shape.
    headers = [h for d in SCAN_DIRS for h in sorted(d.glob("*.h")) if h.name not in EXCLUDE]
    # Anything carrying a HUMAN field is a transcribed fact under QC, whether
    # or not it happens to have a lookup table.
    parsed = [p for p in (parse_header(h) for h in headers) if p["banner"]["human"]]

    if args.status:
        pending = [p for p in parsed if p["banner"]["pending"]]
        if not pending:
            print("all transcribed facts have human sign-off")
            return 0
        print(f"{len(pending)} item(s) awaiting human verification:\n")
        for p in sorted(pending, key=lambda p: p["banner"]["gate"]):
            b = p["banner"]
            count = sum(len(t["entries"]) for t in p["tables"])
            what = f"{count} entries" if count else "header facts"
            print(f"  {b['gate'] or '?':<6} {b['title'] or b['file']:<28} {what:<14} {b['path']}")
        return 1

    text = render(parsed)
    if args.output:
        Path(args.output).write_text(text, encoding="utf-8")
        print(f"wrote {args.output}", file=sys.stderr)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
