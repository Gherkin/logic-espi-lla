// The one translation unit in the tree compiled with -DLOGIC2, and therefore
// the one that is allowed to name FrameV2. It is linked into the plugin only.
//
// docs/PLAN.md section 6, gotcha 3: testlib defines no FrameV2 symbols at all,
// so if this file ever reached a test binary's link that binary would fail to
// link outright. Keeping it to a single small file is what makes that
// impossible by construction rather than by discipline.

#include "FrameV2Sink.h"

#include <AnalyzerResults.h>

namespace espi_saleae
{

void EmitTransactionV2( AnalyzerResults* results, const TransactionSummary& summary )
{
    if( results == nullptr )
        return;

    FrameV2 frame;
    if( !summary.opcode.empty() )
        frame.AddString( "opcode", summary.opcode.c_str() );
    frame.AddBoolean( "truncated", summary.truncated );
    frame.AddBoolean( "error", summary.error );

    results->AddFrameV2( frame, "transaction", summary.start_sample, summary.end_sample );
}

} // namespace espi_saleae
