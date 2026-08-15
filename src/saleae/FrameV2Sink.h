#ifndef ESPI_FRAME_V2_SINK_H
#define ESPI_FRAME_V2_SINK_H

#include <LogicPublicTypes.h>

#include <string>

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

    bool truncated = false;
    bool error = false;
};

void EmitTransactionV2( AnalyzerResults* results, const TransactionSummary& summary );

} // namespace espi_saleae

#endif // ESPI_FRAME_V2_SINK_H
