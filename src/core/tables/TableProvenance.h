#ifndef ESPI_TABLE_PROVENANCE_H
#define ESPI_TABLE_PROVENANCE_H

// ---------------------------------------------------------------------------
//  Provenance and verification bookkeeping for specification-derived tables.
//
//  PRIVATE to espi_core by construction. This directory is attached to the
//  target with `PRIVATE` visibility and is a sibling of the public include
//  root, not a subdirectory of it -- so no test, encoder or simulator target
//  can reach anything in here. That is rule R3, and tests/r3_seam_probe.cpp
//  exists to prove it stays true.
//
//  Why the seam matters: if the code that *builds* test data and the code that
//  *decodes* it share a mapping table, a wrong table round-trips perfectly and
//  the suite reports green. Test fixtures are therefore literal bytes, and the
//  tables below are unreachable from anything that produces them.
//
//  ---------------------------------------------------------------------------
//  Every table in this directory carries a provenance block:
//
//      ESPI_TABLE_BEGIN( VwIndex3,
//          "eSPI Base Spec, Table 10, p.64",
//          "Compatibility Spec, Table 2-13, p.36 (authority)",
//          ESPI_RENDERED( "2026-08-14" ),
//          ESPI_HUMAN_PENDING( "QC-2" ) )
//
//  SOURCE     where the value came from
//  CROSSREF   a second document defining the same table, and which one wins
//  RENDERED   the date the values were checked against a *rendered page image*
//  HUMAN      sign-off date, or the QC gate still owed
//
//  On RENDERED: PDF table extraction is not evidence. The base spec's Table 10
//  arrives from text extraction with its bit-to-name alignment scrambled --
//  rows merged, bit numbers attached to the wrong wire. Extracted text is only
//  ever used to find which page to render; every value is read off the image.
//
//  On HUMAN: a transcription checked only by its own author is checked once.
//  T1 vectors catch a table that drifts from what its author intended; they
//  cannot catch an author who misread the page. Only the human gate closes
//  that, which is why ESPI_HUMAN_PENDING is a visible marker and not a comment.
// ---------------------------------------------------------------------------

namespace espi
{
namespace tables
{

enum class Verification
{
    Pending,  // transcribed, not yet independently read against the spec
    Verified, // signed off at a QC gate
};

struct Provenance
{
    const char* table;
    const char* source;
    const char* crossref;
    const char* rendered;
    Verification status;
    const char* gate; // owed gate when Pending, sign-off date when Verified
};

} // namespace tables
} // namespace espi

#define ESPI_RENDERED( date ) date
#define ESPI_HUMAN_PENDING( gate ) ::espi::tables::Verification::Pending, gate
#define ESPI_HUMAN_VERIFIED( date ) ::espi::tables::Verification::Verified, date

// Declares the provenance record for a table. The record is emitted into a
// translation-unit-local constant so `tools/qc_worksheet.py` can scrape it and
// the build can report which tables are still unverified.
#define ESPI_TABLE_PROVENANCE( name, source, crossref, rendered, ... )                                                              \
    static constexpr ::espi::tables::Provenance kProvenance_##name = { #name, source, crossref, rendered, __VA_ARGS__ }

#endif // ESPI_TABLE_PROVENANCE_H
