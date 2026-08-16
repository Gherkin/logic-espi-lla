#include "EspiAnalyzer.h"

#include "DecodedText.h"
#include "FrameV2Sink.h"
#include "SamplingByteSource.h"

#include "espi/Decode.h"
#include "espi/LinkDecoder.h"
#include "espi/Session.h"

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

// A field's explanatory children in full, on one line, so nothing the decoder
// worked out is lost just because it did not earn its own frame.
//
// Kept separate from the field's own text rather than concatenated onto it:
// they are shown at different bubble widths.
std::string Explanations( const espi::Field& field )
{
    std::string text;
    for( const espi::Field& child : field.children )
    {
        if( !text.empty() )
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

// The same children, compressed to what they resolved to.
//
// The full form is accurate and long: a virtual wire packet spells out every
// masked wire and why it is masked, which is worth having and does not fit in a
// bubble. This is the rung between that and nothing at all --
// "TARGET_BOOT_LOAD_STATUS=high" rather than a sentence about the three wires
// whose valid bits are clear.
//
// Notes are left out: they explain their parent rather than resolving to
// anything, so there is no "=" to write (Decode.h, FieldKind).
std::string CompactExplanations( const espi::Field& field )
{
    std::string text;
    for( const espi::Field& child : field.children )
    {
        if( child.kind == espi::FieldKind::Note )
            continue;

        // Comma separated, because the names have spaces in them: "Virtual
        // Wire Channel Ready=0 Virtual Wire Channel Enable=0" does not show
        // where one ends and the next begins.
        if( !text.empty() )
            text += ", ";
        text += child.name;
        text += '=';

        // What it resolved to, or the value itself when the decoder had nothing
        // to resolve it to. A status bit's text is "bit 0 = 1" with no meaning
        // half, and "PC_FREE=1" is the useful compression of that, so the raw
        // is better here than any part of the string.
        const std::string meaning = MeaningOf( child.text );
        text += meaning.empty() ? std::to_string( child.raw ) : meaning;
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

std::string FrameV2TypeName( const std::string& display_name )
{
    std::string type;
    bool pending_separator = false;
    for( char c : display_name )
    {
        const bool alphanumeric = ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' );
        if( !alphanumeric )
        {
            pending_separator = !type.empty();
            continue;
        }
        if( pending_separator )
        {
            type += '_';
            pending_separator = false;
        }
        type += static_cast<char>( ( c >= 'A' && c <= 'Z' ) ? c - 'A' + 'a' : c );
    }
    return type;
}

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

    // Without this the FrameV2 records below are emitted and dropped on the
    // floor by Logic 2. It goes through the sink seam because testlib has no
    // definition of it -- see FrameV2Sink.h.
    EnableFrameV2( this );

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

    const std::string detail = Explanations( field );
    mResults->AddDecodedField( frame, field.name, field.text, CompactExplanations( field ), detail );

    // The same field again, structured, for the tabular view and protocol
    // search. Both surfaces are fed from one walk so they cannot disagree about
    // what was decoded.
    FieldRecord record;
    record.start_sample = field.span.first;
    record.end_sample = field.span.last;
    record.type = FrameV2TypeName( field.name );
    record.name = field.name;
    record.text = field.text;
    record.detail = detail;
    record.raw = field.raw;
    record.error = field.severity == espi::Severity::Error;
    for( const espi::Field& child : field.children )
    {
        record.parts.emplace_back( FrameV2TypeName( child.name ), child.text.empty() ? child.name : child.text );
    }
    EmitFieldV2( mResults.get(), record );

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

    // The setting is where the session STARTS, not what it is. Nothing on the
    // bus announces the I/O mode, so a capture that opens mid-session needs to
    // be told -- and from there the wire decides, because an accepted
    // SET_CONFIGURATION to 008h or an In-band RESET changes it (espi/Session.h).
    espi::SessionState session( mSettings->mStartingMode );

    for( ;; )
    {
        // Before the chip select rather than after the last one: this is the
        // mode the transaction about to be read was sent in, and on the first
        // pass there is no last one.
        source.SetMode( session.Mode() );

        if( !source.SyncToNextAssertion() )
            return;

        espi::Transaction transaction;
        if( !decoder.Decode( &transaction ) )
            return;

        // At the deassertion edge, which is where the specification puts it and
        // therefore after the whole transaction -- including its response and
        // its CRC -- has been read in the old mode.
        session.Apply( transaction );

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

U32 EspiAnalyzer::GenerateSimulationData( U64 newest_sample_requested, U32 sample_rate,
                                          SimulationChannelDescriptor** simulation_channels )
{
    // GetSimulationSampleRate() is the rate Logic 2 will read the transitions
    // back at, which is not necessarily the device rate passed in beside it.
    // The generator holds both and converts.
    if( !mSimulation.Ready() )
        mSimulation.Initialize( GetSimulationSampleRate(), mSettings.get() );

    return mSimulation.GenerateSimulationData( newest_sample_requested, sample_rate, simulation_channels );
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
