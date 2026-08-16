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
#include "espi/Session.h"

#include "EspiAnalyzer.h"
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
//  T2-d. A waveform whose I/O mode changes partway through.
//
//  Phase 7's exit criterion, and the only place in the suite where following
//  the switch is observable. At T1 a fixture is bytes and the bytes are the
//  same bytes whatever mode carried them, so a state machine that applied the
//  mode one transaction late -- or never -- decodes identically. Here the mode
//  decides how many clocks a byte occupies, so a mistimed switch recovers not
//  a wrong field but no field at all.
//
//  WHAT MAKES IT EVIDENCE. The waveform states each transaction's mode
//  literally, the way the fixture states its bytes; the serializer never looks
//  at what it is laying down. The analyzer is told only the STARTING mode and
//  has to work the rest out from the SET_CONFIGURATION and the RESET it
//  decodes. The expectations are the same hand-written .expected files T1 uses
//  (R2) -- nothing new was authored to match this.
// -------------------------------------------------------------------------

// One transaction of a fixture, and the block of its .expected that goes with
// it. Multiple-transaction .expected files separate their blocks with a ---
// line, which is the test's formatting and not the decoder's.
struct Step
{
    const char* fixture;
    size_t index;
    espi::IoMode mode; // the mode this transaction is ON THE WIRE in
};

std::string ExpectedBlock( const std::string& fixture, size_t index )
{
    bool ok = false;
    const std::string text = ReadFile( VectorPath( ExpectedName( fixture ) ), &ok );
    if( !ok )
    {
        std::fprintf( stderr, "FAIL  cannot read %s\n", ExpectedName( fixture ).c_str() );
        TEST_CHECK( false );
        return {};
    }

    std::istringstream lines( text );
    std::string line;
    std::string block;
    size_t at = 0;
    while( std::getline( lines, line ) )
    {
        if( line == "---" )
        {
            if( at == index )
                return block;
            ++at;
            block.clear();
            continue;
        }
        block += line;
        block += '\n';
    }
    TEST_CHECK_EQ( at, index );
    return block;
}

void TestModeSwitchFollowedMidCapture()
{
    // Single, then Quad, then Single again -- the switch and its undo, because
    // a decoder that latched the first change and never came back would pass a
    // test that only went one way.
    //
    //   1. SET_CONFIGURATION of 008h selecting Quad, ACCEPTed. Sent in Single
    //      I/O: "The SET_CONFIGURATION is completed with the current mode of
    //      operation" (§5.1, p.86).
    //   2. An ordinary GET_CONFIGURATION, now in Quad.
    //   3. An In-band RESET in Quad -- sixteen clocks, which is eight bytes
    //      there, hence reset_quad.espi rather than reset.espi.
    //   4. The same GET_CONFIGURATION again, back in Single. Identical bytes
    //      and identical expected text as step 2, at half the clock rate: it
    //      only comes back if the RESET was followed.
    static const Step kSteps[] = {
        { "set_configuration_io_mode.espi", 0, espi::IoMode::Single },
        { "get_configuration.espi", 0, espi::IoMode::Quad },
        { "reset_quad.espi", 0, espi::IoMode::Quad },
        { "get_configuration.espi", 0, espi::IoMode::Single },
    };

    std::vector<std::vector<FixtureFrame>> loaded;
    espi_test::WaveformBuilder builder( espi::IoMode::Single, espi_test::WaveformGeometry{} );
    std::string want;

    for( const Step& step : kSteps )
    {
        loaded.emplace_back();
        if( !LoadFrames( step.fixture, &loaded.back() ) )
            return;
        if( step.index >= loaded.back().size() )
        {
            std::fprintf( stderr, "FAIL  %s has no transaction %zu\n", step.fixture, step.index );
            TEST_CHECK( false );
            return;
        }

        builder.AddTransaction( loaded.back()[ step.index ], step.mode );

        if( !want.empty() )
            want += "---\n";
        want += ExpectedBlock( step.fixture, step.index );
    }
    builder.Finish();

    MockBus bus;
    bus.Build( builder );

    espi_saleae::SamplingByteSource::Channels wired = bus.Wired();

    // The analyzer is told Single and nothing else. Everything after the first
    // chip select comes from what it decodes.
    espi_saleae::SamplingByteSource source( wired, espi::IoMode::Single );
    espi::LinkDecoder decoder( &source );
    espi::SessionState session( espi::IoMode::Single );

    std::string got;
    for( size_t i = 0; i < sizeof( kSteps ) / sizeof( kSteps[ 0 ] ); ++i )
    {
        source.SetMode( session.Mode() );

        if( !source.SyncToNextAssertion() )
        {
            std::fprintf( stderr, "FAIL  no CS# assertion for transaction %zu\n", i );
            TEST_CHECK( false );
            return;
        }

        espi::Transaction transaction;
        if( !decoder.Decode( &transaction ) )
        {
            std::fprintf( stderr, "FAIL  transaction %zu produced nothing\n", i );
            TEST_CHECK( false );
            return;
        }
        session.Apply( transaction );

        // The mode the session arrived at has to be the mode the waveform was
        // actually laid down in for the NEXT transaction. Checked here as well
        // as through the rendered text because the text of a garbled decode is
        // long and says nothing about which step went wrong.
        if( i + 1 < sizeof( kSteps ) / sizeof( kSteps[ 0 ] ) )
            TEST_CHECK( session.Mode() == kSteps[ i + 1 ].mode );

        if( i != 0 )
            got += "---\n";
        got += espi::Render( transaction );
    }

    if( got != want )
        std::fprintf( stderr, "FAIL  mode switch decode differs\n--- expected ---\n%s--- got ---\n%s-----------\n",
                      want.c_str(), got.c_str() );
    TEST_CHECK( got == want );

    // Single -> Quad -> Single. Rendered text cannot see this: a decoder handed
    // the right mode from the start produces the same words.
    TEST_CHECK_EQ( session.ModeChanges(), 2u );
    TEST_CHECK( session.Mode() == espi::IoMode::Single );
    TEST_CHECK( !session.CrcChecking() ); // the RESET turned it back off
    TEST_CHECK( !session.Uncertain() );
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

    // ---------------------------------------------------------------------
    //  Bubble text, at every width Logic 2 might draw it.
    //
    //  Logic 2 picks the LONGEST string that fits, so what matters is not one
    //  string but the set: is there something worth reading at each width. The
    //  frame order is the one kExpectedTypes states below.
    // ---------------------------------------------------------------------
    auto strings_for = [ & ]( U64 frame ) {
        instance.GenerateBubbleText( frame, wired[ kChanIo0 ], Hexadecimal );
        std::vector<std::string> out;
        for( U64 i = 0; i < results->TotalStringCount(); ++i )
            out.push_back( results->GetString( i ) );
        return out;
    };

    auto contains = []( const std::vector<std::string>& strings, const std::string& wanted ) {
        for( const std::string& s : strings )
        {
            if( s == wanted )
                return true;
        }
        return false;
    };

    auto contains_substring = []( const std::vector<std::string>& strings, const std::string& wanted ) {
        for( const std::string& s : strings )
        {
            if( s.find( wanted ) != std::string::npos )
                return true;
        }
        return false;
    };

    // The opcode: the resolved name on its own is offered, so a bubble with
    // room for nine characters shows what the byte MEANS rather than what it
    // was. Both are there; Logic 2 takes the longest that fits.
    const std::vector<std::string> opcode_strings = strings_for( 0 );
    TEST_CHECK( contains( opcode_strings, "GET_VWIRE" ) );
    TEST_CHECK( contains( opcode_strings, "0x05" ) );

    // The turn-around has no value, only a width -- its raw is 0 and means
    // nothing. Offering "0x00" would put that in the narrowest bubbles, where
    // it would be the only rung that fits.
    const std::vector<std::string> tar_strings = strings_for( 2 );
    TEST_CHECK( contains( tar_strings, "2 clocks" ) );
    TEST_CHECK( !contains( tar_strings, "0x00" ) );

    // The virtual wire data byte: the compressed form of its children sits
    // between the bare value and the full sentence, which is the rung a wide
    // bubble can actually show.
    const std::vector<std::string> data_strings = strings_for( 6 );
    TEST_CHECK( contains( data_strings, "0x88" ) );
    TEST_CHECK( contains_substring( data_strings, "TARGET_BOOT_LOAD_STATUS=high" ) );
    TEST_CHECK( contains_substring( data_strings, "valid bit clear, level not updated" ) );

    // And the note about masked wires is not compressed into a value it does
    // not have -- FieldKind::Note exists to stop "Masked=0" appearing beside a
    // real wire state.
    TEST_CHECK( !contains_substring( data_strings, "Masked=" ) );

    // ---------------------------------------------------------------------
    //  The FrameV2 type names, which docs/PLAN.md section 10 calls the
    //  contract downstream HLAs bind to.
    //
    //  They are derived from the core's display names, so a rename in the core
    //  would silently rewrite the contract. This list is written from
    //  get_vwire.expected -- the same human-written decode T1 uses -- and is in
    //  emission order, so a rename, a reordering or a field that stops being
    //  emitted all fail here.
    // ---------------------------------------------------------------------
    // Every field this fixture emits happens to have a single-word name, so
    // the list below cannot reach the separator rule at all -- deleting it
    // leaves this green. TestFrameV2TypeNames covers that half.
    const char* const kExpectedTypes[] = {
        "opcode", "crc",   "tar",  "response", "count",
        "index",  "data",  "status", "crc",
    };

    const auto& fields = espi_saleae::RecordedFieldsV2();
    TEST_CHECK_EQ( fields.size(), sizeof( kExpectedTypes ) / sizeof( kExpectedTypes[ 0 ] ) );
    for( size_t i = 0; i < fields.size() && i < sizeof( kExpectedTypes ) / sizeof( kExpectedTypes[ 0 ] ); ++i )
    {
        if( fields[ i ].type != kExpectedTypes[ i ] )
        {
            std::fprintf( stderr, "FAIL  FrameV2 field %zu has type '%s', expected '%s'\n", i,
                          fields[ i ].type.c_str(), kExpectedTypes[ i ] );
            TEST_CHECK( false );
        }
    }

    // And the payload is the decode, not a placeholder: the value a human
    // wrote down for the opcode, and a status word whose bits came along.
    if( !fields.empty() )
    {
        // The core formats a field's value and its resolved meaning into one
        // text -- "0x05  GET_VWIRE" -- and Render only prepends the name.
        TEST_CHECK( fields[ 0 ].text.find( "GET_VWIRE" ) != std::string::npos );
        TEST_CHECK( fields[ 0 ].raw == 0x05 );
    }
    for( const auto& field : fields )
    {
        if( field.type != "status" )
            continue;

        TEST_CHECK( field.detail.find( "PC_FREE" ) != std::string::npos );
        TEST_CHECK( field.detail.find( "FLASH_C_FREE" ) != std::string::npos );

        // The same bits again as separate keys. That is the difference between
        // a sentence in a tooltip and something the tabular view can put in a
        // column -- get_vwire.expected lists five set bits for 0x010F.
        TEST_CHECK_EQ( field.parts.size(), size_t( 5 ) );
        bool found_pc_free = false;
        for( const auto& part : field.parts )
        {
            if( part.first == "pc_free" )
                found_pc_free = true;
        }
        TEST_CHECK( found_pc_free );
    }
}

// The type-name derivation on its own, including the case no fixture reaches.
void TestFrameV2TypeNames()
{
    TEST_CHECK( espi_saleae::FrameV2TypeName( "Opcode" ) == "opcode" );
    TEST_CHECK( espi_saleae::FrameV2TypeName( "CRC" ) == "crc" );
    TEST_CHECK( espi_saleae::FrameV2TypeName( "Virtual Wire Packet" ) == "virtual_wire_packet" );
    TEST_CHECK( espi_saleae::FrameV2TypeName( "PC_FREE" ) == "pc_free" );

    // A separator run is one underscore, and neither end of the name carries
    // one -- a leading or trailing underscore in an HLA key is the kind of
    // thing nobody notices until it is in somebody's script.
    TEST_CHECK( espi_saleae::FrameV2TypeName( "Message  Code" ) == "message_code" );
    TEST_CHECK( espi_saleae::FrameV2TypeName( " Tag / Length " ) == "tag_length" );
    TEST_CHECK( espi_saleae::FrameV2TypeName( "" ).empty() );
}

} // namespace

int main()
{
    TestFrameV2TypeNames();
    TestEveryFixtureThroughTheWaveform();
    TestDualAndQuadGeometry();
    TestModeSwitchFollowedMidCapture();
    TestStopsAtChipSelect();
    TestResynchronisesFromMidTransaction();
    TestWorkerThreadEmitsFrames();
    TEST_MAIN_RETURN();
}
