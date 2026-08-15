# logic-espi-lla

A low-level **Intel eSPI** analyzer for Saleae Logic 2.

Decodes eSPI down to individual protocol fields — not just opcodes, but every
header field, virtual wire with its valid-bit pairing, configuration register
bit and status flag, with CRC verification on both phases.

Status: **early.** The protocol core is under construction; the Logic 2 plugin
does not exist yet.

---

## Build

Docker with a Linux daemon is the only requirement.

```bash
docker build -t espi-build .
docker run --rm -v "$PWD":/work espi-build ./scripts/build.sh
```

The core and its tests need no SDK and no network access. `ESPI_BUILD_PLUGIN`
is off by default; turning it on fetches the Saleae Analyzer SDK, pinned to an
exact commit in `cmake/AnalyzerSDK.cmake`.

---

## Layout

```
src/core/include/espi/   public core API -- no Saleae headers, ever
src/core/tables/         spec-derived mapping tables. PRIVATE. See rule R3.
src/core/src/            core implementation
tests/                   test suite; vectors/ holds literal fixtures
tools/                   mutation runner, QC worksheet, out-of-tree CRC reference
```

There is a hard seam between a pure C++ core that never includes a Saleae
header and a thin shell that adapts it to the SDK. Everything with protocol
meaning lives in the core, behind a byte-stream interface a test can trivially
fabricate — so a protocol bug never has to be diagnosed through a synthesized
waveform.

---

## The one thing to understand before contributing

Decoding a protocol from a specification is transcription work, and transcription
that is only ever checked by its own author is checked once. Two rules follow,
and both are load-bearing:

**Tests must not agree with themselves.** Test fixtures are literal bytes; the
test serializer has no protocol knowledge at all; and the specification-derived
mapping tables are made physically unreachable from test code by CMake include
visibility, so a round-trip cannot pass by consulting the same wrong table twice.
`tools/mutate.py` then measures whether the tests constrain anything — a mutation
that survives is a missing test, not a pass.

**Specification tables are read off rendered pages, never off extracted text.**
PDF text extraction mangles tables in ways that look plausible. It has already
turned an eight-bit opcode encoding into nine bits by reading a footnote marker
as a bit. Tables carry a provenance block recording the source page and whether a
human has verified them; `tools/qc_worksheet.py --status` exits nonzero while any
remain unverified.
