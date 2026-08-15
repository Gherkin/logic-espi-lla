#ifndef ESPI_TEST_WAVEFORM_SERIALIZER_H
#define ESPI_TEST_WAVEFORM_SERIALIZER_H

// Turns fixture bytes into a waveform. The T2 counterpart of
// FixtureByteSource, and like it, deliberately ignorant.
//
// IT IS A SERIALIZER, NOT AN eSPI ENCODER -- docs/PLAN.md section 4. It knows
// only "these bytes are the command phase, there is a turn-around here, those
// bytes are the response phase", which is what the fixture file already states
// in literal hex (rule R1). It cannot compute a packet length, look up an
// opcode, or reach src/core/tables/ (rule R3). So when the decoder recovers
// the fixture's bytes from the waveform, nothing in this file helped it decide
// where anything ended.
//
// The one thing it shares with the decoder is bit-to-lane packing, via
// espi::PackByte. docs/PLAN.md section 4 enumerates that as unavoidable -- a
// waveform test is impossible unless the code laying bits down agrees with the
// code picking them up -- and pins it separately in tests/test_lanes.cpp,
// where Pack and Unpack are each checked against Figures 56/57/58 rather than
// against each other.
//
// WAVEFORM MODEL, from the specification:
//
//   - CLK idles low and is low at the CS# assertion edge (section 3, p.21).
//   - Data is launched on the falling edge and sampled on the rising edge, so
//     each bit group is placed a half period before the rising edge that reads
//     it (section 3, p.21).
//   - During the Turn-Around window the lines are driven high for the first
//     clock and then tri-stated, with a weak pull-up holding them high
//     (section 3.3, p.28). Undriven lanes are modelled high throughout for the
//     same reason -- in Single I/O that includes I/O[1] for the whole command
//     phase and I/O[0] for the whole response phase.
//
// The clock keeps toggling through the idle gaps between transactions. That is
// not how a real bus behaves -- Figure 10 stops the clock outside CS# -- but
// the mock throws OutOfDataException the moment a lookahead finds no further
// transition, so a decoder legitimately probing one byte past the end of a
// malformed packet would terminate the whole worker instead of reporting a
// truncated transaction. The clock edges are bounded by CS# in L0, so the
// extra toggles change no decode.

#include "espi/IoMode.h"
#include "espi/LaneCodec.h"
#include "support/FixtureByteSource.h"

#include <MockChannelData.h>
#include <TestInstance.h>

#include <array>
#include <cstdint>
#include <vector>

namespace espi_test
{

// Channel order used throughout the T2 tests.
enum WaveChannel
{
    kChanCs = 0,
    kChanClk,
    kChanIo0,
    kChanIo1,
    kChanIo2,
    kChanIo3,
    kChanCount
};

struct WaveformGeometry
{
    uint32_t half_period = 4; // samples per half clock period
    uint32_t lead_in = 32;    // idle before the first assertion
    uint32_t setup = 16;      // CS# assertion to the first rising edge
    uint32_t hold = 16;       // last falling edge to CS# deassertion
    uint32_t gap = 64;        // idle between transactions
};

class WaveformBuilder
{
  public:
    WaveformBuilder( espi::IoMode mode, const WaveformGeometry& geometry )
        : mMode( mode ), mGeometry( geometry )
    {
        // Everything idles high except the clock: CS# is active low and the
        // lanes sit on their weak pull-ups.
        mInitial[ kChanCs ] = true;
        mInitial[ kChanClk ] = false;
        for( int i = kChanIo0; i <= kChanIo3; ++i )
            mInitial[ i ] = true;
        for( int i = 0; i < kChanCount; ++i )
            mLevel[ i ] = mInitial[ i ];

        mSample = mGeometry.lead_in;
    }

    // One CS#-delimited transaction, laid down exactly as the fixture states
    // it. No length is computed here.
    void AddTransaction( const Frame& frame )
    {
        Set( kChanCs, mSample, false );
        const uint64_t assert_sample = mSample;
        mSample += mGeometry.setup;

        for( uint8_t byte : frame.command )
            EmitByte( byte, espi::Phase::Command );

        if( frame.has_turnaround )
        {
            // Consumed, never interpreted -- lines high.
            for( int i = 0; i < espi::kTurnAroundClocks; ++i )
                EmitClock( espi::LaneBits{ true, true, true, true } );
        }

        for( uint8_t byte : frame.response )
            EmitByte( byte, espi::Phase::Response );

        mSample += mGeometry.hold;
        Set( kChanCs, mSample, true );
        mAssertions.push_back( assert_sample );

        // Idle, with the clock still running -- see the header note.
        const uint64_t gap_end = mSample + mGeometry.gap;
        while( mSample + 2 * mGeometry.half_period <= gap_end )
            EmitClock( espi::LaneBits{ true, true, true, true } );
        mSample = gap_end;
    }

    // Trailing idle so that a lookahead just past the final transaction finds
    // a transition rather than the end of the capture.
    void Finish()
    {
        for( int i = 0; i < 8; ++i )
            EmitClock( espi::LaneBits{ true, true, true, true } );
    }

    BitState InitialState( int channel ) const { return mInitial[ channel ] ? BIT_HIGH : BIT_LOW; }
    const std::vector<uint64_t>& Transitions( int channel ) const { return mTransitions[ channel ]; }
    const std::vector<uint64_t>& AssertionSamples() const { return mAssertions; }

  private:
    void Set( int channel, uint64_t sample, bool level )
    {
        if( mLevel[ channel ] == level )
            return;
        mTransitions[ channel ].push_back( sample );
        mLevel[ channel ] = level;
    }

    // One clock period: lanes settle a half period early, then the rising edge
    // that samples them, then the falling edge.
    void EmitClock( const espi::LaneBits& lanes )
    {
        for( int lane = 0; lane < 4; ++lane )
            Set( kChanIo0 + lane, mSample, lanes[ lane ] );

        mSample += mGeometry.half_period;
        Set( kChanClk, mSample, true );

        mSample += mGeometry.half_period;
        Set( kChanClk, mSample, false );
    }

    void EmitByte( uint8_t value, espi::Phase phase )
    {
        espi::LaneBits packed[ 8 ];
        const size_t clocks = espi::PackByte( mMode, phase, value, packed, 8 );
        const espi::LaneSet active = espi::LanesFor( mMode, phase );

        for( size_t c = 0; c < clocks; ++c )
        {
            // Undriven lanes stay on their pull-ups.
            espi::LaneBits levels{ true, true, true, true };
            for( int k = 0; k < active.count; ++k )
            {
                const uint8_t lane = static_cast<uint8_t>( active.first + k );
                levels[ lane ] = packed[ c ][ lane ];
            }
            EmitClock( levels );
        }
    }

    espi::IoMode mMode;
    WaveformGeometry mGeometry;
    uint64_t mSample = 0;
    bool mInitial[ kChanCount ] = {};
    bool mLevel[ kChanCount ] = {};
    std::vector<uint64_t> mTransitions[ kChanCount ];
    std::vector<uint64_t> mAssertions;
};

// The ONLY way a MockChannelData should be built in this tree.
//
// TestSetInitialBitState must be called before any transition is appended: it
// asserts the transition list is empty, and it is what pushes the dummy
// transition at sample 0 that AdvanceToSample relies on. That function does
// `upper_bound(...) - 1` and only guards the result against end(), so without
// the sample-0 entry a lookup before the first real transition forms
// `begin() - 1` and reads out of bounds. Its own asserts are the only thing
// that would catch it, and they vanish in a release build.
inline AnalyzerTest::MockChannelData* MakeChannelData( AnalyzerTest::Instance* instance, BitState initial,
                                                       const std::vector<uint64_t>& transitions )
{
    auto* data = new AnalyzerTest::MockChannelData( instance );
    data->TestSetInitialBitState( initial );
    for( uint64_t sample : transitions )
        data->TestAppendTransitionAtSamples( sample );
    data->ResetCurrentSample();
    return data;
}

} // namespace espi_test

#endif // ESPI_TEST_WAVEFORM_SERIALIZER_H
