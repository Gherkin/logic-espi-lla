#!/usr/bin/env python3
"""Generate a human QC checklist from the specification-derived tables.

The tables in src/core/tables/ are transcription work, and a transcription
checked only by its author is checked once. This turns them into something a
person can verify against a rendered spec page without reading any C++.

    python3 tools/qc_worksheet.py                 # markdown to stdout
    python3 tools/qc_worksheet.py -o qc.md

Generating the worksheet from the tables is safe precisely because the
worksheet is not the oracle -- the specification is, and the reader is. What
this cannot do is tell you whether a value is right; it tells you exactly
which values to check and where to look them up.

The checklist is a working document, not a record. Tick it off, fix whatever
it turns up, then throw it away -- sign-off state deliberately does not live
in the headers.
"""

import argparse
import re
import sys
from pathlib import Path

# Spec citations are not confined to the private tables directory: the CRC
# parameters and the lane assignment are public API, but they are still
# transcribed facts and still need a human to read them against a page.
SCAN_DIRS = [Path("src/core/tables"), Path("src/core/include/espi")]

# The citation lives in the banner comment above each table.
SOURCE_RE = re.compile(r"^//\s+SOURCE\s+(.*?)\s*$")
# The banner title is the first comment line after the opening divider.
DIVIDER_RE = re.compile(r"^//\s*-{10,}\s*$")
TITLE_RE = re.compile(r"^//\s{2}(\S.*?)\s*$")
TABLE_DEF_RE = re.compile(r"^#define\s+(ESPI_\w*TABLE)\s*\(\s*X\s*\)")
# Entries may carry a trailing block comment, e.g. `X( 0x2, 0 ) /* Reserved */`.
# Capturing it matters: dropping the row would hide the Reserved encoding from
# the very checklist meant to catch a wrong Reserved encoding.
ENTRY_RE = re.compile(r"^\s*X\(\s*(.*?)\s*\)\s*(?:/\*\s*(.*?)\s*\*/)?\s*\\?\s*$")
GROUP_RE = re.compile(r"^\s*/\*\s*-*\s*(.*?)\s*-*\s*\*/\s*\\?\s*$")
# Continuation of the citation: a comment line indented past the keyword
# column and not itself a keyword.
CONTINUATION_RE = re.compile(r"^//\s{9,}(\S.*?)\s*$")


def split_args(text: str) -> list:
    """Split a table row's arguments on top-level commas only.

    A cell may itself contain a parenthesised list -- the packet shape table
    has `ESPI_CMD( Addr16, Data32 )` as a single argument. Splitting naively
    tears that into two cells and the worksheet shows a row that does not
    correspond to anything in the header, which is worse than showing nothing.
    """
    args, depth, current = [], 0, ""
    for ch in text:
        if ch == "," and depth == 0:
            args.append(current.strip())
            current = ""
            continue
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        current += ch
    args.append(current.strip())
    return [a for a in args if a]


def parse_header(path: Path) -> dict:
    lines = path.read_text(encoding="utf-8").splitlines()
    banner = {"title": None, "source": ""}
    tables, current, group = [], None, None
    in_source, after_divider = False, False

    for line in lines:
        m = SOURCE_RE.match(line)
        if m:
            # Keep the FIRST occurrence. A file may cite a second source in a
            # subordinate comment -- IoMode.h cites section 3.10 for the TAR
            # constant -- and that must not displace the banner citation.
            if not banner["source"]:
                banner["source"] = m.group(1)
                in_source = True
            continue

        if in_source and current is None:
            m = CONTINUATION_RE.match(line)
            if m:
                banner["source"] = f"{banner['source']} {m.group(1)}".strip()
                continue
            in_source = False

        if banner["title"] is None:
            if DIVIDER_RE.match(line):
                after_divider = True
                continue
            if after_divider:
                m = TITLE_RE.match(line)
                if m:
                    banner["title"] = m.group(1).strip().rstrip(".")
                after_divider = False

        m = TABLE_DEF_RE.match(line)
        if m:
            current = {"macro": m.group(1), "entries": []}
            tables.append(current)
            group = None
            continue

        if current is not None:
            m = GROUP_RE.match(line)
            if m and m.group(1):
                group = m.group(1)
                continue
            m = ENTRY_RE.match(line)
            if m:
                args = split_args(m.group(1))
                current["entries"].append((group, args, m.group(2) or ""))
                continue
            if line.strip() == "" or not line.rstrip().endswith("\\"):
                if current["entries"]:
                    current = None

    banner["file"] = path.name
    banner["path"] = str(path).replace("\\", "/")
    return {"banner": banner, "tables": tables}


def render(parsed_files: list) -> str:
    out = ["# eSPI mapping table QC worksheet", ""]
    out.append(
        "Each row is a value transcribed from the specification. Check it against "
        "the **rendered page**, not extracted text. Tick the row or correct the table."
    )
    out.append("")

    for p in parsed_files:
        b = p["banner"]
        out.append(f"## {b['title'] or b['file']}")
        out.append("")
        out.append(f"- **Source** — {b['source']}")
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
    args = ap.parse_args()

    headers = [h for d in SCAN_DIRS for h in sorted(d.glob("*.h"))]
    # A SOURCE citation is what marks a header as transcribed from the spec,
    # whether or not it happens to carry a lookup table.
    parsed = [p for p in (parse_header(h) for h in headers) if p["banner"]["source"]]

    text = render(parsed)
    if args.output:
        Path(args.output).write_text(text, encoding="utf-8")
        print(f"wrote {args.output}", file=sys.stderr)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
