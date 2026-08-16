// T2 -- sampling and integration.
//
// The tier that decides whether the analyzer works on a waveform rather than
// on a list of bytes. Everything below L0 has been provable without the SDK;
// this is the first test that needs it.
//
// WHAT MAKES THIS EVIDENCE AND NOT A ROUND TRIP. The fixtures are the same
// literal .espi files T1 uses (rule R1) and the expectations are the same
// hand-written .expected files (rule R2) -- not new artifacts written to match
// whatever this code produces. The serializer lays bytes down as lane
// transitions and knows nothing else; the decoder works out every length from
// the opcode and the shape table behind the R3 seam. So a byte recovered here
// had to survive pack, waveform, sample and unpack, and then still render as
// the text a human wrote out longhand from the specification.
//
// Reusing T1's expectations is deliberate: it costs no new authoring and it
// makes any divergence between the two paths a failure rather than a second
// opinion.

#include "espi/Decode.h"
#include "espi/IoMode.h"
#include "espi/LinkDecoder.h"

#include "EspiAnalyzerResults.h"
#include "EspiAnalyzerSettings.h"
#include "FrameV2SinkRecording.h"
#include "SamplingByteSource.h"

#include "support/FixtureByteSource.h"
#include "support/TestMacros.h"
#include "support/WaveformSerializer.h"

#include <MockChannelData.h>
#include <MockResults.h>
#include <TestInstance.h>

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using espi_test::kChanClk;
using espi_test::kChanCount;
using espi_test::kChanCs;
using espi_test::kChanIo0;

// The SDK has a Frame class of its own and this file sees both. Neither name
// is worth changing, so spell the fixture one out.
using FixtureFrame = espi_test::Frame;

namespace
{

// Every fixture T1 exercises at Single I/O. Listed rather than globbed so that
// a fixture added without a T2 entry is a visible omission.
//
// One T1 fixture is deliberately absent: reset_quad.espi, whose .expected is
// written for Quad I/O because Figure 65's sixteen clocks are eight bytes
// there. It is replayed in TestDualAndQuadGeometry instead.
const char* const kFixtures[] = {
    "get_configuration.espi",        "set_configuration.espi",
    "get_vwire.espi",                "config_oob_channel.espi",
    "get_vwire_boot_done.espi",      "get_vwire_platform_index.espi",
    "config_general.espi",           "config_vwire_max.espi",
    "get_status_vwire_append.espi",  "put_vwire_system_events.espi",
    "get_vwire_system_events.espi",  "vwire_malformed.espi",
    "vwire_remaining_wires.espi",    "put_pc_memory_write32.espi",
    "get_pc_completion.espi",        "put_np_memory_read.espi",
    "get_np_request.espi",           "completion_split.espi",
    "short_cycles.espi",             "cycle_type_coverage.espi",
    "put_pc_ltr_message.espi",       "put_oob_smbus.espi",
    "get_oob_mctp.espi",             "flash_controller_attached.espi",
    "flash_target_attached.espi",    "flash_rpmc.espi",
    "config_flash_rpmc.espi",        "config_device_and_flash.espi",
    "status_all_bits.espi",          "response_errors.espi",
    "wait_state.espi",               "malformed.espi",
    "reset.espi",
};

std::string VectorPath( const std::string& name )
{
    return std::string( ESPI_VECTOR_DIR ) + "/link/" + name;
}

std::string ExpectedName( const std::string& fixture )
{
    return fixture.substr( 0, fixture.size() - 5 ) + ".expected";
}

std::string ReadFile( const std::string& path, bool* ok )
{
    std::ifstream in( path );
    if( !in )
    {
        *ok = false;
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    *ok = true;
    return ss.str();
}

// Holds the six mock channels alive for the duration of one decode.
struct MockBus
{
    AnalyzerTest::Instance instance;
    std::vector<std::unique_ptr<AnalyzerTest::MockChannelData>> channels;

    void Build( const espi_test::WaveformBuilder& builder )
    {
        for( int c = 0; c < kChanCount; ++c )
        {
            channels.emplace_back(
                espi_test::MakeChannelData( &instance, builder.InitialState( c ), builder.Transitions( c ) ) );
        }
    }

    espi_saleae::SamplingByteSource::Channels Wired()
    {
        espi_saleae::SamplingByteSource::Channels wired;
        wired.cs = channels[ kChanCs ].get();
        wired.clk = channels[ kChanClk ].get();
        for( int i = 0; i < 4; ++i )
            wired.io[ i ] = channels[ kChanIo0 + i ].get();
        return wired;
    }
};

bool LoadFrames( const char* fixture, std::vector<FixtureFrame>* frames )
{
    std::string error;
    if( !espi_test::LoadFixture( VectorPath( fixture ), frames, &error ) )
    {
        std::fprintf( stderr, "FAIL  %s\n", error.c_str() );
        TEST_CHECK( false );
        return false;
    }
    if( frames->empty() )
    {
        std::fprintf( stderr, "FAIL  %s contains no transactions\n", fixture );
        TEST_CHECK( false );
        return false;
    }
    return true;
}

// Serialize the fixture into a waveform, sample it back through L0, and render
// what the core made of it.
std::string DecodeThroughWaveform( const char* fixture, espi::IoMode mode )
{
    std::vector<FixtureFrame> frames;
    if( !LoadFrames( fixture, &frames ) )
        return {};

    espi_test::WaveformBuilder builder( mode, espi_test::WaveformGeometry{} );
    for( const FixtureFrame& frame : frames )
        builder.AddTransaction( frame );
    builder.Finish();

    MockBus bus;
    bus.Build( builder );

    espi_saleae::SamplingByteSource::Channels wired = bus.Wired();
    espi_saleae::SamplingByteSource source( wired, mode );
    espi::LinkDecoder decoder( &source );

    std::string out;
    for( size_t i = 0; i < frames.size(); ++i )
    {
        if( !source.SyncToNextAssertion() )
        {
            std::fprintf( stderr, "FAIL  %s: no CS# assertion for transaction %zu\n", fixture, i );
            TEST_CHECK( false );
            break;
        }

        espi::Transaction transaction;
        if( !decoder.Decode( &transaction ) )
        {
            std::fprintf( stderr, "FAIL  %s: transaction %zu produced nothing\n", fixture, i );
            TEST_CHECK( false );
            break;
        }

        if( i != 0 )
            out += "---\n";
        out += espi::Render( transaction );
    }
    return out;
}

void CheckWaveformMatchesExpected( const char* fixture, espi::IoMode mode )
{
    const std::string got = DecodeThroughWaveform( fixture, mode );

    bool ok = false;
    const std::string want = ReadFile( VectorPath( ExpectedName( fixture ) ), &ok );
    if( !ok )
    {
        std::fprintf( stderr, "FAIL  cannot read %s\n", ExpectedName( fixture ).c_str() );
        TEST_CHECK( false );
        return;
    }

    if( got != want )
        std::fprintf( stderr,
                      "FAIL  %s decoded from a waveform differs from %s\n"
                      "--- expected ---\n%s--- got ---\n%s-----------\n",
                      fixture, ExpectedName( fixture ).c_str(), want.c_str(), got.c_str() );
    TEST_CHECK( got == want );
}

// -------------------------------------------------------------------------
//  T2-a. Every fixture, sampled off a Single I/O waveform.
// -------------------------------------------------------------------------
void TestEveryFixtureThroughTheWaveform()
{
    for( const char* fixture : kFixtures )
        CheckWaveformMatchesExpected( fixture, espi::IoMode::Single );
}

// -------------------------------------------------------------------------
//  T2-c. The same bytes in Dual and Quad.
//
//  A byte occupies four clocks in Dual and two in Quad, so a wrong lane order
//  or a wrong clocks-per-byte count produces garbage rather than a subtle
//  shift. The expectation is unchanged: the same bytes went onto the wire, so
//  the same decode has to come back off it.
//
//  This is the geometry only. Following a mid-capture mode switch is phase 7.
// -------------------------------------------------------------------------
void TestDualAndQuadGeometry()
{
    CheckWaveformMatchesExpected( "get_vwire.espi", espi::IoMode::Dual );
    CheckWaveformMatchesExpected( "get_configuration.espi", espi::IoMode::Dual );
    CheckWaveformMatchesExpected( "get_vwire.espi", espi::IoMode::Quad );
    CheckWaveformMatchesExpected( "get_configuration.espi", espi::IoMode::Quad );
    CheckWaveformMatchesExpected( "put_pc_memory_write32.espi", espi::IoMode::Quad );

    // The one fixture whose expectation is mode specific rather than mode
    // independent. Section 8.3.2's RESET is defined in CLOCKS -- sixteen of
    // them, Figure 65 p.123 -- so the same command is two bytes in Single I/O
    // and eight in Quad, and its decode names a different byte count for each.
    // reset_quad.espi holds the Quad form and is not in kFixtures above.
    CheckWaveformMatchesExpected( "reset_quad.espi", espi::IoMode::Quad );
}

// -------------------------------------------------------------------------
//  L0's own boundary rules, which no .expected file can express.
// -------------------------------------------------------------------------

// A transaction that never turns around must not read the next one's bytes.
void TestStopsAtChipSelect()
{
    std::vector<FixtureFrame> frames;
    if( !LoadFrames( "get_configuration.espi", &frames ) )
        return;

    espi_test::WaveformBuilder builder( espi::IoMode::Single, espi_test::WaveformGeometry{} );
    builder.AddTransaction( frames[ 0 ] );
    builder.AddTransaction( frames[ 0 ] );
    builder.Finish();

    MockBus bus;
    bus.Build( builder );
    espi_saleae::SamplingByteSource::Channels wired = bus.Wired();
    espi_saleae::SamplingByteSource source( wired, espi::IoMode::Single );

    TEST_CHECK( source.SyncToNextAssertion() );
    TEST_CHECK( source.Active() );

    // Read far past the end of the first transaction. It must run out at the
    // CS# boundary rather than walking into the second one.
    const size_t transaction_bytes = frames[ 0 ].command.size() + frames[ 0 ].response.size();
    size_t read = 0;
    espi::StreamByte byte;
    while( source.ReadByte( espi::Phase::Command, &byte ) )
    {
        ++read;
        if( read > transaction_bytes + 8 )
            break;
    }

    // Exact, not an upper bound: "<=" would also pass if ReadByte gave up
    // immediately, which is the failure this test exists to catch. The count
    // is the whole transaction's clocks divided by eight -- the two TAR clocks
    // cannot complete a ninth byte, so they contribute nothing.
    TEST_CHECK_EQ( read, transaction_bytes );
    TEST_CHECK( !source.Active() );

    // And the next assertion still decodes cleanly -- one exhausted
    // transaction must not desynchronise the one after it.
    TEST_CHECK( source.SyncToNextAssertion() );
    TEST_CHECK( source.Active() );
    TEST_CHECK( source.ReadByte( espi::Phase::Command, &byte ) );
    TEST_CHECK_EQ( byte.value, frames[ 0 ].command[ 0 ] );
}

// A capture that starts mid-transaction has no way to know how far in it is,
// so L0 skips to the next whole one rather than decoding a fragment.
void TestResynchronisesFromMidTransaction()
{
    std::vector<FixtureFrame> frames;
    if( !LoadFrames( "get_configuration.espi", &frames ) )
        return;

    espi_test::WaveformBuilder builder( espi::IoMode::Single, espi_test::WaveformGeometry{} );
    builder.AddTransaction( frames[ 0 ] );
    builder.AddTransaction( frames[ 0 ] );
    builder.Finish();

    MockBus bus;
    bus.Build( builder );

    // Start the capture with CS# already low, as if the recording began inside
    // the first transaction.
    bus.channels[ kChanCs ]->ResetCurrentSample( builder.AssertionSamples()[ 0 ] + 1 );

    espi_saleae::SamplingByteSource::Channels wired = bus.Wired();
    espi_saleae::SamplingByteSource source( wired, espi::IoMode::Single );

    TEST_CHECK( source.SyncToNextAssertion() );
    // It must have landed on the SECOND assertion, not the fragment of the first.
    TEST_CHECK_EQ( source.AssertSample(), builder.AssertionSamples()[ 1 ] );

    espi::LinkDecoder decoder( &source );
    espi::Transaction transaction;
    TEST_CHECK( decoder.Decode( &transaction ) );
    TEST_CHECK( !transaction.truncated );
}

// -------------------------------------------------------------------------
//  T2-b. The whole shell, through the SDK's own entry point.
// -------------------------------------------------------------------------
void TestWorkerThreadEmitsFrames()
{
    std::vector<FixtureFrame> frames;
    if( !LoadFrames( "get_vwire.espi", &frames ) )
        return;

    espi_test::WaveformBuilder builder( espi::IoMode::Single, espi_test::WaveformGeometry{} );
    for( const FixtureFrame& frame : frames )
        builder.AddTransaction( frame );
    builder.Finish();

    espi_saleae::ClearRecordedTransactionsV2();

    AnalyzerTest::Instance instance( "eSPI" );
    instance.SetSampleRate( 100 * 1000 * 1000 );

    auto* settings = static_cast<espi_saleae::EspiAnalyzerSettings*>( instance.GetSettings() );
    TEST_CHECK( settings != nullptr );
    if( settings == nullptr )
        return;

    Channel wired[ kChanCount ];
    for( int c = 0; c < kChanCount; ++c )
        wired[ c ] = Channel( 0, static_cast<U32>( c ), DIGITAL_CHANNEL );

    settings->mChipSelect = wired[ kChanCs ];
    settings->mClock = wired[ kChanClk ];
    for( int i = 0; i < 4; ++i )
        settings->mIo[ i ] = wired[ kChanIo0 + i ];
    settings->mStartingMode = espi::IoMode::Single;

    std::vector<std::unique_ptr<AnalyzerTest::MockChannelData>> channels;
    for( int c = 0; c < kChanCount; ++c )
    {
        channels.emplace_back(
            espi_test::MakeChannelData( &instance, builder.InitialState( c ), builder.Transitions( c ) ) );
        instance.SetChannelData( wired[ c ], channels.back().get() );
    }

    // The worker runs until the capture is exhausted; that is how the harness
    // terminates it, and it is the same shape as Logic 2 blocking for more data.
    const auto result = instance.RunAnalyzerWorker();
    TEST_CHECK( result == AnalyzerTest::Instance::WorkerRanOutOfData );

    auto* results = AnalyzerTest::MockResultData::MockFromResults( instance.GetResults() );
    TEST_CHECK( results != nullptr );
    if( results == nullptr )
        return;

    const U64 frame_count = results->TotalFrameCount();
    TEST_CHECK( frame_count > 0 );

    // Gotcha 7: frames may not overlap and may not be shorter than two samples.
    S64 previous_end = -1;
    for( U64 i = 0; i < frame_count; ++i )
    {
        const Frame f = results->GetFrame( i );
        if( f.mEndingSampleInclusive - f.mStartingSampleInclusive < 1 )
            std::fprintf( stderr, "FAIL  frame %llu spans %lld..%lld\n", (unsigned long long)i,
                          (long long)f.mStartingSampleInclusive, (long long)f.mEndingSampleInclusive );
        TEST_CHECK( f.mEndingSampleInclusive - f.mStartingSampleInclusive >= 1 );
        TEST_CHECK( f.mStartingSampleInclusive > previous_end );
        previous_end = f.mEndingSampleInclusive;
    }

    // The FrameV2 seam: one record per transaction, produced by the recording
    // implementation because this binary is not compiled with LOGIC2.
    const auto& recorded = espi_saleae::RecordedTransactionsV2();
    TEST_CHECK_EQ( recorded.size(), frames.size() );
    if( !recorded.empty() )
    {
        TEST_CHECK( recorded[ 0 ].opcode.find( "GET_VWIRE" ) != std::string::npos );
        TEST_CHECK( !recorded[ 0 ].truncated );
        TEST_CHECK( !recorded[ 0 ].error );
    }

    // Bubble text has to come back out for the frames that went in.
    instance.GenerateBubbleText( 0, wired[ kChanIo0 ], Hexadecimal );
    TEST_CHECK( results->TotalStringCount() > 0 );
    if( results->TotalStringCount() > 0 )
        TEST_CHECK( !results->GetString( 0 ).empty() );
}

} // namespace

int main()
{
    TestEveryFixtureThroughTheWaveform();
    TestDualAndQuadGeometry();
    TestStopsAtChipSelect();
    TestResynchronisesFromMidTransaction();
    TestWorkerThreadEmitsFrames();
    TEST_MAIN_RETURN();
}
