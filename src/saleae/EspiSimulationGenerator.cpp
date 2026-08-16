#include "EspiSimulationGenerator.h"

#include <AnalyzerHelpers.h>

namespace espi_saleae
{
namespace
{

// The eSPI clock the demo waveform runs at.
//
// 20 MHz is encoding 0h of the Operating Frequency field at offset 08h bits
// 22:20 (pp.95-96) -- the lowest frequency the specification defines, and so
// the one that survives the widest range of simulation sample rates. The
// waveform is only as close to it as an integer number of samples per half
// period allows; see HalfPeriodFor.
constexpr U32 kSimulationClockHz = 20 * 1000 * 1000;

// Samples per half clock period, rounded to nearest.
//
// Two is the floor: a half period shorter than that has no room for a rising
// and a falling edge on distinct samples, and the SDK forbids frames shorter
// than two samples (docs/PLAN.md section 6, gotcha 7). A sample rate too low
// for the requested frequency therefore produces a slower demo clock rather
// than an unrepresentable waveform.
U32 HalfPeriodFor( U32 sample_rate )
{
    const U32 rounded = ( sample_rate + kSimulationClockHz ) / ( 2 * kSimulationClockHz );
    return rounded < 2 ? 2 : rounded;
}

} // namespace

void EspiSimulationGenerator::Initialize( U32 simulation_sample_rate, EspiAnalyzerSettings* settings )
{
    mSettings = settings;
    if( settings == nullptr )
        return;

    mMode = settings->mStartingMode;
    mSimulationRateHz = simulation_sample_rate;
    mHalfPeriod = HalfPeriodFor( simulation_sample_rate );

    // Single I/O needs I/O[1] as well as I/O[0]: the response phase is on a
    // different wire from the command phase (Figure 56, p.88). Without every
    // lane the mode uses there is no waveform to draw, so the generator stays
    // empty and GenerateSimulationData reports no channels -- the same answer
    // it gave before this phase existed, and an honest one.
    const int lanes_needed = mMode == espi::IoMode::Single ? 2 : espi::BitsPerClock( mMode );
    if( settings->mChipSelect == UNDEFINED_CHANNEL || settings->mClock == UNDEFINED_CHANNEL )
        return;
    for( int i = 0; i < lanes_needed; ++i )
    {
        if( settings->mIo[ i ] == UNDEFINED_CHANNEL )
            return;
    }

    // Everything idles high except the clock: CS# is active low and the lanes
    // sit on their weak pull-ups.
    auto add = [ this ]( Channel channel, BitState initial ) -> SimulationChannelDescriptor* {
        SimulationChannelDescriptor* descriptor = &mChannels[ mCount++ ];
        descriptor->SetChannel( channel );
        descriptor->SetSampleRate( mSimulationRateHz );
        descriptor->SetInitialBitState( initial );
        return descriptor;
    };

    mCs = add( settings->mChipSelect, BIT_HIGH );
    mClk = add( settings->mClock, BIT_LOW );
    for( int i = 0; i < 4; ++i )
    {
        // Lanes the mode does not use are simulated too when the capture has
        // them, so the demo shows the same six channels the settings dialog
        // asked for rather than four channels and two gaps.
        if( settings->mIo[ i ] != UNDEFINED_CHANNEL )
            mIo[ i ] = add( settings->mIo[ i ], BIT_HIGH );
    }

    // Lead-in, so the first CS# assertion is not at sample 0. Offline that is
    // more than cosmetic: MockChannelData reserves sample 0 for the dummy
    // transition TestSetInitialBitState pushes, and asserts every appended
    // transition is strictly later.
    EmitIdleClocks( 2 );
}

void EspiSimulationGenerator::AdvanceAll( U32 samples )
{
    for( U32 i = 0; i < mCount; ++i )
        mChannels[ i ].Advance( samples );
    mSample += samples;
}

void EspiSimulationGenerator::SetLevel( SimulationChannelDescriptor* channel, bool high )
{
    channel->TransitionIfNeeded( high ? BIT_HIGH : BIT_LOW );
}

void EspiSimulationGenerator::EmitClock( const espi::LaneBits& lanes )
{
    for( int lane = 0; lane < 4; ++lane )
    {
        if( mIo[ lane ] != nullptr )
            SetLevel( mIo[ lane ], lanes[ lane ] );
    }

    AdvanceAll( mHalfPeriod );
    SetLevel( mClk, true );

    AdvanceAll( mHalfPeriod );
    SetLevel( mClk, false );
}

void EspiSimulationGenerator::EmitByte( uint8_t value, espi::Phase phase )
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

void EspiSimulationGenerator::EmitIdleClocks( U32 count )
{
    for( U32 i = 0; i < count; ++i )
        EmitClock( espi::LaneBits{ true, true, true, true } );
}

void EspiSimulationGenerator::EmitTransaction( const SimTransaction& transaction )
{
    // Section 3, p.21: the clock is low at the assertion edge. It always is
    // here, because a clock period ends on the falling edge.
    SetLevel( mCs, false );
    AdvanceAll( 2 * mHalfPeriod );

    for( uint8_t byte : transaction.command )
        EmitByte( byte, espi::Phase::Command );

    if( transaction.turnaround )
    {
        // Consumed, never interpreted -- lines high (section 3.3, p.28).
        for( int i = 0; i < espi::kTurnAroundClocks; ++i )
            EmitClock( espi::LaneBits{ true, true, true, true } );
    }

    for( uint8_t byte : transaction.response )
        EmitByte( byte, espi::Phase::Response );

    AdvanceAll( 2 * mHalfPeriod );
    SetLevel( mCs, true );
    ++mEmitted;

    // Idle, with the clock still running -- see the header note.
    EmitIdleClocks( 4 );
}

U32 EspiSimulationGenerator::GenerateSimulationData( U64 newest_sample_requested, U32 sample_rate,
                                                     SimulationChannelDescriptor** simulation_channels )
{
    if( simulation_channels == nullptr || mCount == 0 )
        return 0;

    const std::vector<SimTransaction>& script = SimulationScript();
    if( !script.empty() )
    {
        // The request is in device samples and the waveform is in simulation
        // samples. The helper converts by integer division, so it must not be
        // handed a simulation rate above the device rate -- that divides by
        // zero. Equal rates need no conversion at all, which is the case every
        // test here takes.
        U64 target = newest_sample_requested;
        if( mSimulationRateHz != 0 && sample_rate > mSimulationRateHz )
            target = AnalyzerHelpers::AdjustSimulationTargetSample( newest_sample_requested, sample_rate,
                                                                    mSimulationRateHz );

        // Whole transactions only. Logic 2 asks for a sample number, not a
        // packet count, and stopping mid-packet would put a fragment on screen
        // that the decoder would rightly report as truncated.
        while( mSample <= target )
        {
            EmitTransaction( script[ mCursor ] );
            mCursor = ( mCursor + 1 ) % script.size();
        }
    }

    *simulation_channels = mChannels.data();
    return mCount;
}

} // namespace espi_saleae
