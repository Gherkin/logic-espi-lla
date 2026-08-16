#include "EspiAnalyzerResults.h"

#include <AnalyzerHelpers.h>

#include <cstdio>

namespace espi_saleae
{

EspiAnalyzerResults::EspiAnalyzerResults() = default;
EspiAnalyzerResults::~EspiAnalyzerResults() = default;

U64 EspiAnalyzerResults::AddDecodedField( const Frame& frame, const std::string& name, const std::string& text,
                                          const std::string& detail )
{
    const U64 index = AddFrame( frame );

    // AddFrame returns the index it landed at, so size the table to match
    // rather than assuming it grew by exactly one.
    if( mText.size() <= index )
        mText.resize( index + 1 );
    mText[ index ] = FieldText{ name, text, detail };

    return index;
}

const EspiAnalyzerResults::FieldText* EspiAnalyzerResults::TextFor( U64 frame_index ) const
{
    if( frame_index >= mText.size() )
        return nullptr;
    return &mText[ frame_index ];
}

// The raw value, in whichever base the user picked in Logic 2's toolbar.
//
// Frames carry it: AddDecodedField puts espi::Field::raw in mData1 and its
// significant bit count in mType, so nothing extra has to be stored to format
// it here. Ignoring display_base -- as this did until the first look at a real
// screen -- makes that toolbar control do nothing.
std::string EspiAnalyzerResults::NumberText( U64 frame_index, DisplayBase display_base )
{
    const Frame frame = GetFrame( frame_index );
    char buffer[ 128 ];
    AnalyzerHelpers::GetNumberString( frame.mData1, display_base, frame.mType, buffer, sizeof( buffer ) );
    return buffer;
}

// ---------------------------------------------------------------------------
//  Bubble text.
//
//  Logic 2 draws the LONGEST of these strings that fits the bubble at the
//  current zoom, so the set has to span the widths a field is actually drawn
//  at. The rungs run value-first, because when only a few characters fit, the
//  value is the half worth showing: a bubble reading "Opcode" tells a reader
//  nothing they cannot see from the column it is in, while "0x21" or
//  "GET_CONFIGURATION" is the decode.
//
//  That was the whole of the first defect T5 found -- the shortest rung used to
//  be the field NAME, so a bubble that could not fit the full text showed the
//  label and hid the answer.
// ---------------------------------------------------------------------------
void EspiAnalyzerResults::GenerateBubbleText( U64 frame_index, Channel& /*channel*/, DisplayBase display_base )
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

    const std::string number = NumberText( frame_index, display_base );
    const std::string value = entry->text.empty() ? entry->name : entry->text;

    AddResultString( number.c_str() );
    AddResultString( value.c_str() );
    AddResultString( entry->name.c_str(), ": ", value.c_str() );
    if( !entry->detail.empty() )
        AddResultString( entry->name.c_str(), ": ", value.c_str(), entry->detail.c_str() );
}

void EspiAnalyzerResults::GenerateFrameTabularText( U64 frame_index, DisplayBase /*display_base*/ )
{
    ClearTabularText();

    const FieldText* entry = TextFor( frame_index );
    if( entry == nullptr )
        return;

    AddTabularText( entry->name.c_str(), ": ", entry->text.c_str(), entry->detail.c_str() );
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
