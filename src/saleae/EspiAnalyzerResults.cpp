#include "EspiAnalyzerResults.h"

#include <AnalyzerHelpers.h>

#include <cstdio>

namespace espi_saleae
{

EspiAnalyzerResults::EspiAnalyzerResults() = default;
EspiAnalyzerResults::~EspiAnalyzerResults() = default;

U64 EspiAnalyzerResults::AddDecodedField( const Frame& frame, const std::string& name, const std::string& text )
{
    const U64 index = AddFrame( frame );

    // AddFrame returns the index it landed at, so size the table to match
    // rather than assuming it grew by exactly one.
    if( mText.size() <= index )
        mText.resize( index + 1 );
    mText[ index ] = FieldText{ name, text };

    return index;
}

const EspiAnalyzerResults::FieldText* EspiAnalyzerResults::TextFor( U64 frame_index ) const
{
    if( frame_index >= mText.size() )
        return nullptr;
    return &mText[ frame_index ];
}

void EspiAnalyzerResults::GenerateBubbleText( U64 frame_index, Channel& /*channel*/, DisplayBase /*display_base*/ )
{
    ClearResultStrings();

    const FieldText* entry = TextFor( frame_index );
    if( entry == nullptr )
    {
        // A frame with no text is a bug in the emission path, not something to
        // paper over with a plausible-looking number.
        AddResultString( "?" );
        return;
    }

    // Longest first is the convention the SDK expects: it picks the longest
    // string that fits the zoom level.
    AddResultString( entry->name.c_str(), ": ", entry->text.c_str() );
    AddResultString( entry->text.c_str() );
    AddResultString( entry->name.c_str() );
}

void EspiAnalyzerResults::GenerateFrameTabularText( U64 frame_index, DisplayBase /*display_base*/ )
{
    ClearTabularText();

    const FieldText* entry = TextFor( frame_index );
    if( entry == nullptr )
        return;

    AddTabularText( entry->name.c_str(), ": ", entry->text.c_str() );
}

void EspiAnalyzerResults::GenerateExportFile( const char* file, DisplayBase /*display_base*/,
                                              U32 /*export_type_user_id*/ )
{
    void* handle = AnalyzerHelpers::StartFile( file );
    if( handle == nullptr )
        return;

    const char header[] = "Start sample,End sample,Field,Value\n";
    AnalyzerHelpers::AppendToFile( reinterpret_cast<const U8*>( header ), sizeof( header ) - 1, handle );

    const U64 total = GetNumFrames();
    for( U64 i = 0; i < total; ++i )
    {
        const Frame frame = GetFrame( i );
        const FieldText* entry = TextFor( i );

        std::string line = std::to_string( frame.mStartingSampleInclusive );
        line += ',';
        line += std::to_string( frame.mEndingSampleInclusive );
        line += ',';
        line += entry != nullptr ? entry->name : std::string();
        line += ',';
        line += entry != nullptr ? entry->text : std::string();
        line += '\n';

        AnalyzerHelpers::AppendToFile( reinterpret_cast<const U8*>( line.data() ),
                                       static_cast<U32>( line.size() ), handle );

        if( UpdateExportProgressAndCheckForCancel( i, total ) )
            break;
    }

    AnalyzerHelpers::EndFile( handle );
}

// Packet and transaction grouping is presentation, which docs/PLAN.md schedules
// for phase 8. Emitting a placeholder string here would look like a feature.
void EspiAnalyzerResults::GeneratePacketTabularText( U64 /*packet_id*/, DisplayBase /*display_base*/ )
{
    ClearTabularText();
}

void EspiAnalyzerResults::GenerateTransactionTabularText( U64 /*transaction_id*/, DisplayBase /*display_base*/ )
{
    ClearTabularText();
}

} // namespace espi_saleae
