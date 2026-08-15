#include "EspiAnalyzer.h"

#include "FrameV2Sink.h"
#include "SamplingByteSource.h"

#include "espi/Decode.h"
#include "espi/LinkDecoder.h"

#include <cassert>

namespace espi_saleae
{
namespace
{

// True when at least one child covers samples its parent does not -- i.e. the
// children are further bytes rather than an explanation of this one. See the
// note on EmitField.
bool ChildrenAreSeparateBytes( const espi::Field& field )
{
    for( const espi::Field& child : field.children )
    {
        if( child.span.first != field.span.first || child.span.last != field.span.last )
            return true;
    }
    return false;
}

// A field is worth a frame only if it covers real samples. The core uses a
// default-constructed ByteSpan for things that never appeared on the wire as
// their own bytes -- WAIT_STATE runs are recorded that way -- and the SDK
// forbids frames shorter than two samples (gotcha 7).
bool Emittable( const espi::ByteSpan& span )
{
    return span.last > span.first;
}

// Fold a field's explanatory children into one line, so nothing the decoder
// worked out is lost just because it did not earn its own frame.
std::string TextWithExplanations( const espi::Field& field )
{
    std::string text = field.text;
    for( const espi::Field& child : field.children )
    {
        text += "; ";
        text += child.name;
        if( !child.text.empty() )
        {
            text += ' ';
            text += child.text;
        }
    }
    return text;
}

U8 FlagsFor( espi::Severity severity )
{
    if( severity == espi::Severity::Error )
        return DISPLAY_AS_ERROR_FLAG;
    if( severity == espi::Severity::Warning )
        return DISPLAY_AS_WARNING_FLAG;
    return 0;
}

} // namespace

EspiAnalyzer::EspiAnalyzer() : mSettings( new EspiAnalyzerSettings() )
{
    SetAnalyzerSettings( mSettings.get() );
}

EspiAnalyzer::~EspiAnalyzer()
{
    KillThread();
}

void EspiAnalyzer::SetupResults()
{
    mResults.reset( new EspiAnalyzerResults() );
    SetAnalyzerResults( mResults.get() );

    // Bubbles land on the lanes, which is where the bytes are.
    for( int i = 0; i < 4; ++i )
    {
        if( mSettings->mIo[ i ] != UNDEFINED_CHANNEL )
            mResults->AddChannelBubblesWillAppearOn( mSettings->mIo[ i ] );
    }
}

void EspiAnalyzer::EmitField( const espi::Field& field, U64* previous_end )
{
    if( !field.children.empty() && ChildrenAreSeparateBytes( field ) )
    {
        for( const espi::Field& child : field.children )
            EmitField( child, previous_end );
        return;
    }

    if( !Emittable( field.span ) )
        return;

    // Frames may not overlap. Spans come from strictly increasing clock edges
    // and are emitted in read order, so this should hold by construction --
    // assert rather than silently drop, because a violation means the tree
    // walk is wrong, not the data.
    assert( field.span.first > *previous_end || *previous_end == 0 );

    Frame frame;
    frame.mStartingSampleInclusive = static_cast<S64>( field.span.first );
    frame.mEndingSampleInclusive = static_cast<S64>( field.span.last );
    frame.mData1 = field.raw;
    frame.mData2 = 0;
    frame.mType = field.bit_width;
    frame.mFlags = FlagsFor( field.severity );

    mResults->AddDecodedField( frame, field.name, TextWithExplanations( field ) );
    *previous_end = field.span.last;
}

void EspiAnalyzer::WorkerThread()
{
    SamplingByteSource::Channels channels;
    channels.cs = GetAnalyzerChannelData( mSettings->mChipSelect );
    channels.clk = GetAnalyzerChannelData( mSettings->mClock );
    for( int i = 0; i < 4; ++i )
    {
        if( mSettings->mIo[ i ] != UNDEFINED_CHANNEL )
            channels.io[ i ] = GetAnalyzerChannelData( mSettings->mIo[ i ] );
    }

    if( channels.cs == nullptr || channels.clk == nullptr )
        return;

    SamplingByteSource source( channels, mSettings->mStartingMode );
    espi::LinkDecoder decoder( &source );

    for( ;; )
    {
        if( !source.SyncToNextAssertion() )
            return;

        espi::Transaction transaction;
        if( !decoder.Decode( &transaction ) )
            return;

        U64 previous_end = 0;
        for( const espi::Field& field : transaction.fields )
            EmitField( field, &previous_end );

        TransactionSummary summary;
        summary.start_sample = source.AssertSample();
        summary.end_sample = source.DeassertSample();
        summary.truncated = transaction.truncated;
        summary.error = transaction.HasError();
        if( const espi::Field* opcode = transaction.Find( "Opcode" ) )
            summary.opcode = opcode->text;
        EmitTransactionV2( mResults.get(), summary );

        mResults->CommitPacketAndStartNewPacket();
        mResults->CommitResults();
        ReportProgress( source.DeassertSample() );
        CheckIfThreadShouldExit();
    }
}

U32 EspiAnalyzer::GenerateSimulationData( U64 /*newest_sample_requested*/, U32 /*sample_rate*/,
                                          SimulationChannelDescriptor** /*simulation_channels*/ )
{
    // Phase 5. Returning zero channels is the honest answer until the
    // generator exists -- and note that the SDK's own group API cannot be used
    // to build one, because SimulationChannelDescriptorGroup::AdvanceAll() is
    // an empty stub offline (docs/PLAN.md section 6, gotcha 1).
    return 0;
}

U32 EspiAnalyzer::GetMinimumSampleRateHz()
{
    // The fastest eSPI clock the specification defines is 66 MHz -- the
    // Operating Frequency encoding at offset 08h bits 22:20, pp.95-96, whose
    // top defined value is 66 MHz. Four samples per clock is the usual SDK
    // convention for reliably finding both edges.
    return 4 * 66 * 1000 * 1000;
}

const char* EspiAnalyzer::GetAnalyzerName() const
{
    return "eSPI";
}

bool EspiAnalyzer::NeedsRerun()
{
    return false;
}

} // namespace espi_saleae

extern "C" {

ANALYZER_EXPORT const char* __cdecl GetAnalyzerName()
{
    return "eSPI";
}

ANALYZER_EXPORT Analyzer* __cdecl CreateAnalyzer()
{
    return new espi_saleae::EspiAnalyzer();
}

ANALYZER_EXPORT void __cdecl DestroyAnalyzer( Analyzer* analyzer )
{
    delete analyzer;
}
}
