// T3 -- the closed loop.
//
// Demo mode is the one surface where this analyzer is both ends of the wire:
// GenerateSimulationData() draws a waveform and WorkerThread() reads it back.
// That makes it the easiest place in the tree to write a test that agrees with
// itself, so it is worth being explicit about what each check is worth.
//
// WHAT IS EVIDENCE HERE. The generator lays down literal bytes and nothing
// else -- it cannot compute a packet length, look up an opcode, or reach
// src/core/tables/ (rules R1 and R3). The decoder recovers the phase split,
// every length and every field from the opcode and the shape table behind that
// seam. And the text each transaction is compared against is the .expected
// file a human wrote out longhand for the fixture the script was transcribed
// from (rule R2) -- not a recording of what this run produced. So a byte here
// has to survive pack, simulated transitions, the bridge, L0 sampling and the
// core, and still render as the words somebody wrote from the specification.
//
// WHAT IS NOT. The generator's waveform model -- clock low at the assertion
// edge, launch on the falling edge, sample on the rising one -- is written
// from the same reading of section 3 p.21 as L0's. The two agreeing says
// nothing about whether that reading of the page is right;
// tests/support/WaveformSerializer.h carries the same caveat and the same
// answer, which is that only a second human reader closes it.
//
// So the claim T3 supports is narrow and worth having: the waveform that ships
// in demo mode is one this analyzer decodes, and the script it replays is the
// traffic the fixtures say it is.

#include "espi/Decode.h"
#include "espi/IoMode.h"
#include "espi/LinkDecoder.h"
#include "espi/Session.h"

#include "EspiAnalyzerSettings.h"
#include "EspiSimulationGenerator.h"
#include "FrameV2SinkRecording.h"
#include "SamplingByteSource.h"
#include "SimulationScript.h"

#include "support/FixtureByteSource.h"
#include "support/SimulationBridge.h"
#include "support/TestMacros.h"

#include <MockChannelData.h>
#include <MockResults.h>
#include <TestInstance.h>

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{

// One channel per eSPI signal, numbered as T2 numbers them.
enum Wire
{
    kWireCs = 0,
    kWireClk,
    kWireIo0,
    kWireIo1,
    kWireIo2,
    kWireIo3,
    kWireCount
};

// 100 MHz, as T2 uses. The generator turns that into six samples per eSPI
// clock.
const U32 kSampleRate = 100 * 1000 * 1000;

// A sample rate too low for the demo clock, which makes the generator clamp to
// two samples per half period -- the tightest waveform it can draw. In Quad a
// byte is two clocks, so the narrowest frame this run emits is 4 samples wide
// against the SDK's minimum of 2 (docs/PLAN.md section 6, gotcha 7). The same
// measurement on the Single I/O run below is 42. That is not the floor itself,
// but it is an order of magnitude closer to it than anything else in this
// suite reaches: T2's geometry is eight samples per clock throughout.
const U32 kMarginalSampleRate = 24 * 1000 * 1000;

// Enough for more than one pass of the script, so the wrap back to the first
// transaction is exercised rather than assumed -- and, on the Single I/O run,
// so the mode excursion is traversed whole rather than left half done at the
// end of the waveform.
const U64 kSimulatedSamples = 12000;

Channel WireChannel( U32 index )
{
    return Channel( 0, index, DIGITAL_CHANNEL );
}

void ConfigureSettings( espi_saleae::EspiAnalyzerSettings* settings, espi::IoMode mode, int lanes )
{
    settings->mChipSelect = WireChannel( kWireCs );
    settings->mClock = WireChannel( kWireClk );
    for( int i = 0; i < 4; ++i )
        settings->mIo[ i ] = ( i < lanes ) ? WireChannel( kWireIo0 + i ) : UNDEFINED_CHANNEL;
    settings->mStartingMode = mode;
}

std::string VectorPath( const std::string& name )
{
    return std::string( ESPI_VECTOR_DIR ) + "/link/" + name;
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

// An .expected file holds one block per transaction, separated by a line of
// three dashes -- the same shape T1 and T2 build when they render a multi
// transaction fixture.
std::vector<std::string> SplitTransactions( const std::string& text )
{
    static const std::string kSeparator = "---\n";
    std::vector<std::string> blocks;
    size_t start = 0;
    for( ;; )
    {
        const size_t at = text.find( kSeparator, start );
        if( at == std::string::npos || ( at != 0 && text[ at - 1 ] != '\n' ) )
            break;
        blocks.push_back( text.substr( start, at - start ) );
        start = at + kSeparator.size();
    }
    blocks.push_back( text.substr( start ) );
    return blocks;
}

// The decode a human wrote for the fixture this script entry was transcribed
// from. Reading it here rather than recording what the decoder produced is
// rule R2, and it is the only reason the comparison means anything.
std::string ExpectedRender( const espi_saleae::SimTransaction& transaction )
{
    const std::string fixture( transaction.fixture );
    const std::string expected_name = fixture.substr( 0, fixture.size() - 5 ) + ".expected";

    bool ok = false;
    const std::string text = ReadFile( VectorPath( expected_name ), &ok );
    if( !ok )
    {
        std::fprintf( stderr, "FAIL  cannot read %s\n", expected_name.c_str() );
        TEST_CHECK( false );
        return {};
    }

    const std::vector<std::string> blocks = SplitTransactions( text );
    if( transaction.index < 0 || static_cast<size_t>( transaction.index ) >= blocks.size() )
    {
        std::fprintf( stderr, "FAIL  %s has no transaction %d\n", expected_name.c_str(), transaction.index );
        TEST_CHECK( false );
        return {};
    }
    return blocks[ static_cast<size_t>( transaction.index ) ];
}

// Generator plus bridge: the simulated waveform, wired up the way an analyzer
// reads it.
struct SimulatedBus
{
    espi_saleae::EspiSimulationGenerator generator;
    std::unique_ptr<AnalyzerTest::MockChannelData> wires[ kWireCount ];
    U32 count = 0;

    // Drives the generator exactly as EspiAnalyzer::GenerateSimulationData
    // does -- and deliberately NOT through AnalyzerTest::Instance::RunSimulation,
    // which reads the returned array as an array of pointers while Logic 2
    // reads it as an array of descriptors. The two agree only for a single
    // channel analyzer; see the note on EspiSimulationGenerator::GenerateSimulationData.
    void Build( AnalyzerTest::Instance* instance, espi_saleae::EspiAnalyzerSettings* settings, U64 samples,
                U32 sample_rate )
    {
        generator.Initialize( sample_rate, settings );

        SimulationChannelDescriptor* descriptors = nullptr;
        count = generator.GenerateSimulationData( samples, sample_rate, &descriptors );
        if( count == 0 || descriptors == nullptr )
            return;

        for( U32 i = 0; i < count; ++i )
        {
            const U32 index = descriptors[ i ].GetChannel().mChannelIndex;
            if( index >= kWireCount )
            {
                std::fprintf( stderr, "FAIL  simulated channel %u is not one of the six signals\n", index );
                TEST_CHECK( false );
                continue;
            }
            wires[ index ].reset( espi_test::BridgeSimulatedChannel( instance, &descriptors[ i ] ) );
        }
    }

    espi_saleae::SamplingByteSource::Channels Wired()
    {
        espi_saleae::SamplingByteSource::Channels wired;
        wired.cs = wires[ kWireCs ].get();
        wired.clk = wires[ kWireClk ].get();
        for( int i = 0; i < 4; ++i )
            wired.io[ i ] = wires[ kWireIo0 + i ].get();
        return wired;
    }
};

// Every transaction the simulated waveform carries, rendered.
//
// The loop is EspiAnalyzer::WorkerThread's: the session decides what mode each
// chip select is read in, and each decoded transaction is folded back into it
// at the deassertion edge. Nothing here is told what the generator laid down.
std::vector<std::string> DecodeEverything( espi_saleae::SamplingByteSource* source, espi::SessionState* session )
{
    espi::LinkDecoder decoder( source );
    std::vector<std::string> rendered;
    try
    {
        for( ;; )
        {
            source->SetMode( session->Mode() );

            if( !source->SyncToNextAssertion() )
                break;

            espi::Transaction transaction;
            if( !decoder.Decode( &transaction ) )
                break;
            session->Apply( transaction );

            rendered.push_back( espi::Render( transaction ) );
        }
    }
    catch( const AnalyzerTest::OutOfDataException& )
    {
        // The waveform ended. That is how a capture finishes, offline and in
        // Logic 2 alike, and it is not a failure.
    }
    return rendered;
}

// -------------------------------------------------------------------------
//  The bridge itself, before anything protocol shaped is put through it.
//
//  Both faults it works around are silent -- a stretched waveform still
//  decodes into something, and a dropped final transition only shortens the
//  last transaction -- so they are pinned here on three transitions where the
//  right answer can be written down.
// -------------------------------------------------------------------------
void TestBridgeReproducesEveryTransition()
{
    AnalyzerTest::Instance instance;

    SimulationChannelDescriptor descriptor;
    Channel channel = WireChannel( kWireCs );
    descriptor.SetChannel( channel );
    descriptor.SetSampleRate( kSampleRate );
    descriptor.SetInitialBitState( BIT_HIGH );

    descriptor.Advance( 10 );
    descriptor.Transition(); // 10
    descriptor.Advance( 10 );
    descriptor.Transition(); // 20
    descriptor.Advance( 15 );
    descriptor.Transition(); // 35
    descriptor.Advance( 20 );

    std::unique_ptr<AnalyzerTest::MockChannelData> data(
        espi_test::BridgeSimulatedChannel( &instance, &descriptor ) );

    const U64 kEdges[] = { 10, 20, 35 };
    const BitState kStates[] = { BIT_LOW, BIT_HIGH, BIT_LOW };

    TEST_CHECK( data->GetBitState() == BIT_HIGH );
    for( int i = 0; i < 3; ++i )
    {
        try
        {
            // Absolute, not relative. docs/PLAN.md section 3's snippet would
            // put edge 1 at 30 and edge 2 at 65.
            TEST_CHECK_EQ( data->GetSampleOfNextEdge(), kEdges[ i ] );
            data->AdvanceToNextEdge();
            TEST_CHECK_EQ( data->GetSampleNumber(), kEdges[ i ] );
            TEST_CHECK( data->GetBitState() == kStates[ i ] );
        }
        catch( const AnalyzerTest::OutOfDataException& )
        {
            // The harness never yields a channel's last transition, so this is
            // where a bridge that trusts its loop runs out one edge early.
            std::fprintf( stderr, "FAIL  transition %d (sample %llu) is missing from the bridged channel\n", i,
                          (unsigned long long)kEdges[ i ] );
            TEST_CHECK( false );
            break;
        }
    }
}

// A lane the generator never drives -- I/O[2] and I/O[3] in Single I/O -- has
// no transitions at all, which is the case the harness throws on rather than
// reporting.
void TestBridgeAcceptsAChannelThatNeverMoves()
{
    AnalyzerTest::Instance instance;

    SimulationChannelDescriptor descriptor;
    Channel channel = WireChannel( kWireIo3 );
    descriptor.SetChannel( channel );
    descriptor.SetSampleRate( kSampleRate );
    descriptor.SetInitialBitState( BIT_HIGH );
    descriptor.Advance( 500 );

    std::unique_ptr<AnalyzerTest::MockChannelData> data(
        espi_test::BridgeSimulatedChannel( &instance, &descriptor ) );
    TEST_CHECK( data != nullptr );
    TEST_CHECK( data->GetBitState() == BIT_HIGH );
}

// -------------------------------------------------------------------------
//  The script is the fixtures.
//
//  The generator ships its bytes compiled in, because the plugin cannot read
//  tests/vectors when Logic 2 has loaded it. This is what stops the two
//  drifting: the script's provenance fields name a fixture and a transaction
//  inside it, and here that claim is checked byte for byte.
// -------------------------------------------------------------------------
void TestScriptMatchesTheFixturesItCites()
{
    for( const espi_saleae::SimTransaction& entry : espi_saleae::SimulationScript() )
    {
        std::vector<espi_test::Frame> frames;
        std::string error;
        if( !espi_test::LoadFixture( VectorPath( entry.fixture ), &frames, &error ) )
        {
            std::fprintf( stderr, "FAIL  %s\n", error.c_str() );
            TEST_CHECK( false );
            continue;
        }

        if( entry.index < 0 || static_cast<size_t>( entry.index ) >= frames.size() )
        {
            std::fprintf( stderr, "FAIL  %s has no transaction %d\n", entry.fixture, entry.index );
            TEST_CHECK( false );
            continue;
        }

        const espi_test::Frame& frame = frames[ static_cast<size_t>( entry.index ) ];
        if( frame.command != entry.command || frame.response != entry.response ||
            frame.has_turnaround != entry.turnaround )
        {
            std::fprintf( stderr, "FAIL  the simulation script has drifted from %s transaction %d\n", entry.fixture,
                          entry.index );
            TEST_CHECK( false );
        }
    }
}

// -------------------------------------------------------------------------
//  T3 proper: generate, sample it back, and read what came out.
// -------------------------------------------------------------------------
void CheckSimulationDecodesAsTheScriptStates( espi::IoMode mode, int lanes, U32 sample_rate )
{
    espi_saleae::EspiAnalyzerSettings settings;
    ConfigureSettings( &settings, mode, lanes );

    AnalyzerTest::Instance instance;
    SimulatedBus bus;
    bus.Build( &instance, &settings, kSimulatedSamples, sample_rate );
    TEST_CHECK_EQ( bus.count, static_cast<U32>( 2 + lanes ) );

    espi_saleae::SamplingByteSource::Channels wired = bus.Wired();
    espi_saleae::SamplingByteSource source( wired, mode );

    // The session is the analyzer's, not the generator's: it is told the
    // starting mode and works the rest out from the SET_CONFIGURATION and the
    // RESET it decodes. The generator was told every mode outright.
    espi::SessionState session( mode );
    const std::vector<std::string> rendered = DecodeEverything( &source, &session );

    // What this run could actually draw, which is not always the whole script.
    const std::vector<espi_saleae::SimTransaction>& script = bus.generator.Script();

    // Which of the two it is, asserted rather than inferred from the run.
    // Everything below compares rendered text against the script it was handed,
    // so a filter that quietly dropped the excursion would leave every one of
    // those comparisons passing -- on a shorter script, decoding a waveform
    // that never changed mode. This is the only check that would notice.
    const bool draws_excursion = ( mode == espi::IoMode::Single && lanes == 4 );
    const size_t whole = espi_saleae::SimulationScript().size();
    TEST_CHECK_EQ( script.size(), draws_excursion ? whole : whole - 3 );

    // Every transaction that was drawn has to come back out. Comparing the
    // rendered text alone cannot see a lost one -- there is simply one block
    // fewer to compare -- and losing the last one is exactly what a bridge
    // that trusts the harness does.
    TEST_CHECK_EQ( rendered.size(), (size_t)bus.generator.TransactionsEmitted() );

    // More than one pass, so a generator that stops after the first script
    // entry -- or wraps to the wrong one -- fails here rather than looking
    // fine on a short capture.
    if( rendered.size() <= script.size() )
    {
        std::fprintf( stderr, "FAIL  %zu samples produced %zu transactions; the %zu entry script did not wrap\n",
                      (size_t)kSimulatedSamples, rendered.size(), script.size() );
        TEST_CHECK( false );
        return;
    }

    for( size_t i = 0; i < rendered.size(); ++i )
    {
        const espi_saleae::SimTransaction& entry = script[ i % script.size() ];
        const std::string want = ExpectedRender( entry );
        if( rendered[ i ] != want )
        {
            std::fprintf( stderr,
                          "FAIL  simulated transaction %zu (%s, %s #%d) differs\n"
                          "--- expected ---\n%s--- got ---\n%s-----------\n",
                          i, entry.summary, entry.fixture, entry.index, want.c_str(), rendered[ i ].c_str() );
            TEST_CHECK( false );
            return;
        }
    }

    // The mode really moved, and moved back. Rendered text cannot see this:
    // every transaction above decodes to the same words whichever geometry
    // carried it, so an analyzer that ignored the switch entirely would be
    // caught by the bytes it failed to recover, and one that was handed the
    // right mode all along would not be caught at all.
    //
    // At least twice because the run makes more than one pass: Single to Quad
    // at the SET_CONFIGURATION, Quad back to Single at the RESET that closes
    // the loop. Without the second the script would wrap into the wrong mode.
    if( draws_excursion )
        TEST_CHECK( session.ModeChanges() >= 2 );
    else
        TEST_CHECK_EQ( session.ModeChanges(), 0u );
}

void TestSimulationDecodesAsTheScriptStates()
{
    CheckSimulationDecodesAsTheScriptStates( espi::IoMode::Single, 4, kSampleRate );

    // The same bytes at the other two geometries. Every transaction in the
    // script is mode independent -- none of them is RESET, whose length is
    // defined in clocks -- so the decode has to come back identical.
    CheckSimulationDecodesAsTheScriptStates( espi::IoMode::Dual, 4, kSampleRate );
    CheckSimulationDecodesAsTheScriptStates( espi::IoMode::Quad, 4, kSampleRate );

    // Single I/O with I/O[2] and I/O[3] left unconnected, which is what a real
    // two-lane capture looks like. The generator must simulate four channels
    // and no more.
    CheckSimulationDecodesAsTheScriptStates( espi::IoMode::Single, 2, kSampleRate );

    // And the narrowest waveform the generator will draw. Demo mode does not
    // get to choose its sample rate: Logic 2 hands one over, and it can be
    // below what GetMinimumSampleRateHz asks for.
    CheckSimulationDecodesAsTheScriptStates( espi::IoMode::Quad, 4, kMarginalSampleRate );
}

// A mode whose lanes are not all assigned has no waveform to draw. Returning
// nothing is the honest answer; drawing a Quad waveform on two lanes is not.
void TestGeneratorRefusesAnIncompleteChannelSet()
{
    espi_saleae::EspiAnalyzerSettings settings;
    ConfigureSettings( &settings, espi::IoMode::Quad, 2 );

    espi_saleae::EspiSimulationGenerator generator;
    generator.Initialize( kSampleRate, &settings );

    SimulationChannelDescriptor* descriptors = nullptr;
    TEST_CHECK_EQ( generator.GenerateSimulationData( kSimulatedSamples, kSampleRate, &descriptors ), U32( 0 ) );
}

// -------------------------------------------------------------------------
//  The whole shell over its own simulation -- which is what demo mode runs.
// -------------------------------------------------------------------------
void CheckWorkerThreadRunsOnItsOwnSimulation( espi::IoMode mode, U32 sample_rate )
{
    espi_saleae::ClearRecordedTransactionsV2();

    AnalyzerTest::Instance instance( "eSPI" );
    instance.SetSampleRate( sample_rate );

    auto* settings = static_cast<espi_saleae::EspiAnalyzerSettings*>( instance.GetSettings() );
    TEST_CHECK( settings != nullptr );
    if( settings == nullptr )
        return;

    // The analyzer's own settings object, so the generator and the worker are
    // looking at one set of channels rather than two that happen to agree.
    ConfigureSettings( settings, mode, 4 );

    SimulatedBus bus;
    bus.Build( &instance, settings, kSimulatedSamples, sample_rate );
    for( U32 i = 0; i < kWireCount; ++i )
    {
        if( bus.wires[ i ] )
            instance.SetChannelData( WireChannel( i ), bus.wires[ i ].get() );
    }

    const auto result = instance.RunAnalyzerWorker();
    TEST_CHECK( result == AnalyzerTest::Instance::WorkerRanOutOfData );

    auto* results = AnalyzerTest::MockResultData::MockFromResults( instance.GetResults() );
    TEST_CHECK( results != nullptr );
    if( results == nullptr )
        return;

    // Gotcha 7: frames may not overlap and may not be shorter than two samples.
    const U64 frame_count = results->TotalFrameCount();
    TEST_CHECK( frame_count > 0 );
    S64 previous_end = -1;
    for( U64 i = 0; i < frame_count; ++i )
    {
        const Frame f = results->GetFrame( i );
        TEST_CHECK( f.mEndingSampleInclusive - f.mStartingSampleInclusive >= 1 );
        TEST_CHECK( f.mStartingSampleInclusive > previous_end );
        previous_end = f.mEndingSampleInclusive;
    }

    // One FrameV2 record per simulated transaction, each naming an opcode that
    // the human written decode of that transaction also names. Nothing here
    // invents a name: the record carries what the core resolved and the
    // .expected file carries what a person read off Table 2.
    const std::vector<espi_saleae::SimTransaction>& script = bus.generator.Script();
    const auto& recorded = espi_saleae::RecordedTransactionsV2();
    TEST_CHECK( recorded.size() > script.size() );
    TEST_CHECK_EQ( recorded.size(), (size_t)bus.generator.TransactionsEmitted() );

    for( size_t i = 0; i < recorded.size(); ++i )
    {
        const espi_saleae::SimTransaction& entry = script[ i % script.size() ];
        TEST_CHECK( !recorded[ i ].truncated );
        TEST_CHECK( !recorded[ i ].error );

        const std::string want = ExpectedRender( entry );
        if( recorded[ i ].opcode.empty() || want.find( recorded[ i ].opcode ) == std::string::npos )
        {
            std::fprintf( stderr, "FAIL  transaction %zu reports opcode '%s', which %s does not mention\n", i,
                          recorded[ i ].opcode.c_str(), entry.fixture );
            TEST_CHECK( false );
            return;
        }
    }
}

void TestWorkerThreadRunsOnItsOwnSimulation()
{
    CheckWorkerThreadRunsOnItsOwnSimulation( espi::IoMode::Single, kSampleRate );

    // Quad at the marginal rate is where a per-field frame is at its narrowest
    // -- a byte is two clocks and a clock is four samples -- so it is the one
    // run where the no-overlap and minimum-width checks above have anything to
    // push against.
    CheckWorkerThreadRunsOnItsOwnSimulation( espi::IoMode::Quad, kMarginalSampleRate );
}

} // namespace

int main()
{
    TestBridgeReproducesEveryTransition();
    TestBridgeAcceptsAChannelThatNeverMoves();
    TestScriptMatchesTheFixturesItCites();
    TestSimulationDecodesAsTheScriptStates();
    TestGeneratorRefusesAnIncompleteChannelSet();
    TestWorkerThreadRunsOnItsOwnSimulation();
    TEST_MAIN_RETURN();
}
