// The one translation unit in the tree compiled with -DLOGIC2, and therefore
// the one that is allowed to name FrameV2. It is linked into the plugin only.
//
// docs/PLAN.md section 6, gotcha 3: testlib defines no FrameV2 symbols at all,
// so if this file ever reached a test binary's link that binary would fail to
// link outright. Keeping it to a single small file is what makes that
// impossible by construction rather than by discipline.

#include "FrameV2Sink.h"

#include <Analyzer.h>
#include <AnalyzerResults.h>

namespace espi_saleae
{
namespace
{

// Keys EmitFieldV2 writes itself. A decoded field whose child happens to be
// named one of these would otherwise overwrite the record's own value, which
// would be a decode quietly replaced by a detail.
const char* const kReservedKeys[] = { "name", "value", "detail", "raw", "error" };

bool IsReserved( const std::string& key )
{
    for( const char* reserved : kReservedKeys )
    {
        if( key == reserved )
            return true;
    }
    return false;
}

} // namespace

void EnableFrameV2( Analyzer* analyzer )
{
    if( analyzer != nullptr )
        analyzer->UseFrameV2();
}

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

void EmitFieldV2( AnalyzerResults* results, const FieldRecord& field )
{
    if( results == nullptr || field.type.empty() )
        return;

    FrameV2 frame;
    frame.AddString( "name", field.name.c_str() );
    frame.AddString( "value", field.text.c_str() );
    if( !field.detail.empty() )
        frame.AddString( "detail", field.detail.c_str() );
    frame.AddInteger( "raw", static_cast<S64>( field.raw ) );
    if( field.error )
        frame.AddBoolean( "error", true );

    for( const auto& part : field.parts )
    {
        if( part.first.empty() || IsReserved( part.first ) )
            continue;
        frame.AddString( part.first.c_str(), part.second.c_str() );
    }

    results->AddFrameV2( frame, field.type.c_str(), field.start_sample, field.end_sample );
}

} // namespace espi_saleae
