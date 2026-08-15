// The offline half of the FrameV2 seam. Linked into test binaries instead of
// FrameV2SinkLogic2.cpp, and compiled WITHOUT -DLOGIC2, so it never names
// FrameV2 and never needs a symbol testlib does not have.
//
// docs/PLAN.md section 6, gotcha 3 recommends recording rather than building
// tests with the FrameV2 path compiled out: the rich key/value payload is the
// eventual deliverable, so a test needs to be able to see that a record was
// produced even while the payload itself is still phase 8's business.

#include "FrameV2SinkRecording.h"

namespace espi_saleae
{
namespace
{

std::vector<TransactionSummary>& Records()
{
    static std::vector<TransactionSummary> records;
    return records;
}

} // namespace

// Nothing to enable offline: there is no Logic 2 to tell, and testlib defines
// no Analyzer::UseFrameV2 to call even if there were.
void EnableFrameV2( Analyzer* /*analyzer*/ )
{
}

void EmitTransactionV2( AnalyzerResults* /*results*/, const TransactionSummary& summary )
{
    Records().push_back( summary );
}

const std::vector<TransactionSummary>& RecordedTransactionsV2()
{
    return Records();
}

void ClearRecordedTransactionsV2()
{
    Records().clear();
}

} // namespace espi_saleae
