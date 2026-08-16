#ifndef ESPI_FRAME_V2_SINK_H
#define ESPI_FRAME_V2_SINK_H

#include <LogicPublicTypes.h>

#include <string>
#include <utility>
#include <vector>

class Analyzer;
class AnalyzerResults;

namespace espi_saleae
{

// ---------------------------------------------------------------------------
//  The FrameV2 seam.
//
//  WHY THIS EXISTS IN PHASE 4 AT ALL. docs/PLAN.md section 6, gotcha 3: the
//  offline harness has zero FrameV2 support. FrameV2 is declared only behind
//  #ifdef LOGIC2 in AnalyzerResults.h and defined nowhere in testlib/, so any
//  translation unit that names it cannot be in a test binary's link. That is a
//  BUILD fact, and getting the build shape wrong is expensive to unpick later
//  -- so the seam is cut now, in phase 4.
//
//  What is NOT settled here is the payload. docs/PLAN.md section 10 makes the
//  FrameV2 type names the contract downstream HLAs bind to, and schedules
//  naming them for phase 8 precisely because renaming afterwards is the
//  expensive mistake. So this carries one record per transaction and no
//  speculative key vocabulary. Phase 8 owns what goes in it.
//
//  Nothing in this header mentions FrameV2, which is what lets every other
//  shell source file compile once and link into both binaries.
//
//  Exactly one definition of EmitTransactionV2 is linked:
//    - FrameV2SinkLogic2.cpp     plugin only, the single TU compiled with LOGIC2
//    - FrameV2SinkRecording.cpp  test binaries, records instead of emitting
// ---------------------------------------------------------------------------

struct TransactionSummary
{
    U64 start_sample = 0;
    U64 end_sample = 0;

    // Opcode name as the core resolved it, or empty when the transaction did
    // not get far enough to name one. Opcode names come from Table 2 and are
    // already transcribed and QC'd, so this is not new vocabulary.
    std::string opcode;

    // What this transaction does to the session, or empty for the great
    // majority that do nothing.
    //
    // WHY IT IS HERE AND NOT ON A FIELD. The core puts the same statement in
    // the decode tree as a Session block, but that block carries no sample span
    // -- the bytes that caused it are already drawn as the Data field or as
    // Register Reset -- so it draws no bubble and reaches no tabular row. This
    // is a property of the whole chip select frame, which is exactly what this
    // record already is.
    //
    // Without it the one thing phase 7 added is invisible on screen: the
    // waveform after a switch is a quarter the width and still decodes, and
    // nothing says why.
    std::string session;

    bool truncated = false;
    bool error = false;
};

// One decoded field, for the tabular view, protocol search and export.
//
// WHY THIS EXISTS. The transaction summary above is one row per CS# frame, and
// the first look at a real screen showed what that costs: the table listed
// type, start, duration, error, truncated and opcode, and there was no way to
// check an address, a status word or a CRC against the specification without
// hovering every bubble in turn. A decode nobody can read is not a decode.
//
// `type` is the FrameV2 type name, which docs/PLAN.md section 10 calls the
// contract downstream HLAs bind to. It is derived from the field's display name
// -- see TypeNameFor() in EspiAnalyzer.cpp -- which keeps one vocabulary rather
// than two, at the cost of coupling the contract to a display string. The
// coupling is deliberate and it is pinned by a test, so a rename shows up as a
// failure rather than as a silently broken HLA.
struct FieldRecord
{
    U64 start_sample = 0;
    U64 end_sample = 0;

    std::string type; // "opcode", "crc", "status" -- the HLA contract
    std::string name; // display name, as the core wrote it
    std::string text; // the formatted value
    std::string detail; // explanatory children, folded onto one line
    U64 raw = 0;
    bool error = false;

    // The same explanatory children, one key each, so a status word arrives as
    // sixteen named bits rather than as a sentence. `detail` is what a bubble
    // can show and this is what the tabular view and protocol search can use.
    //
    // Keys named like the record's own fields are dropped rather than silently
    // overwriting them; see kReservedKeys in FrameV2SinkLogic2.cpp.
    std::vector<std::pair<std::string, std::string>> parts;
};

// Declare that this analyzer produces FrameV2 results.
//
// Analyzer.h line 42: "call this function if your analyzer produces FrameV2
// results". Without it AddFrameV2 output does not surface in Logic 2 at all,
// so the records below would be emitted and silently dropped.
//
// It has to come through this seam rather than being called directly. The
// declaration is NOT guarded by LOGIC2 -- so it compiles anywhere -- but
// testlib defines no Analyzer::UseFrameV2, so a direct call from any shared
// shell source would leave the test binary with an undefined symbol. That is
// the trap this seam exists to avoid, in the one shape where the header gives
// no warning.
void EnableFrameV2( Analyzer* analyzer );

void EmitTransactionV2( AnalyzerResults* results, const TransactionSummary& summary );

void EmitFieldV2( AnalyzerResults* results, const FieldRecord& field );

} // namespace espi_saleae

#endif // ESPI_FRAME_V2_SINK_H
