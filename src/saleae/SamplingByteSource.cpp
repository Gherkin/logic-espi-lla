#include "SamplingByteSource.h"

#include <AnalyzerChannelData.h>

namespace espi_saleae
{

SamplingByteSource::SamplingByteSource( const Channels& channels, espi::IoMode mode )
    : mChannels( channels ), mMode( mode )
{
}

bool SamplingByteSource::SyncToNextAssertion()
{
    if( mChannels.cs == nullptr || mChannels.clk == nullptr )
        return false;

    // CS# is active low, so an assertion is a falling edge. If we are sitting
    // inside an assertion -- either because the capture started that way or
    // because the previous transaction stopped early -- step over its
    // deassertion first, so that what we arm on is always a real assertion
    // edge and never the middle of a packet.
    if( mChannels.cs->GetBitState() == BIT_LOW )
        mChannels.cs->AdvanceToNextEdge();

    mChannels.cs->AdvanceToNextEdge();

    mAssertSample = mChannels.cs->GetSampleNumber();

    // Look the deassertion up once, without advancing CS#. Every clock edge in
    // the transaction is then bounds-checked against a number we already have,
    // which is what keeps Active() cheap and const and keeps the decoder from
    // reading past the boundary.
    mDeassertSample = mChannels.cs->GetSampleOfNextEdge();

    // Section 3, p.21: the serial clock is low at the assertion edge, so the
    // first rising edge after this point is the first data bit.
    mChannels.clk->AdvanceToAbsPosition( mAssertSample );

    // No falling edge has been seen inside this transaction yet, so the first
    // byte's span starts at its own first rising edge. There is genuinely
    // nowhere better: the clock is low from the assertion edge to that rising
    // edge, so the first bit was launched somewhere in a window whose start is
    // not observable.
    mLaunchEdge = 0;

    mArmed = true;
    return true;
}

uint64_t SamplingByteSource::LaunchOf( uint64_t first_edge ) const
{
    const bool inside = mLaunchEdge > mAssertSample && mLaunchEdge < first_edge;
    return inside ? mLaunchEdge : first_edge;
}

bool SamplingByteSource::NextSamplingEdge( uint64_t* sample )
{
    if( !mArmed )
        return false;

    for( ;; )
    {
        const U64 edge = mChannels.clk->GetSampleOfNextEdge();
        if( edge >= mDeassertSample )
        {
            mArmed = false;
            return false;
        }

        mChannels.clk->AdvanceToNextEdge();
        if( mChannels.clk->GetBitState() == BIT_HIGH )
        {
            *sample = edge;
            return true;
        }

        // A falling edge is where data is launched, not where it is read -- so
        // it is not a sampling point, and it IS where the bit about to be
        // sampled first appeared on the wire. Keep it for the span.
        mLaunchEdge = edge;
    }
}

bool SamplingByteSource::SampleLanes( espi::Phase phase, uint64_t sample, espi::LaneBits* out )
{
    const espi::LaneSet lanes = espi::LanesFor( mMode, phase );

    *out = espi::LaneBits{ false, false, false, false };
    for( int k = 0; k < lanes.count; ++k )
    {
        const uint8_t lane = static_cast<uint8_t>( lanes.first + k );
        AnalyzerChannelData* channel = mChannels.io[ lane ];
        if( channel == nullptr )
        {
            // The mode needs a lane the capture does not have. Refusing is the
            // only honest answer -- treating an absent lane as a constant zero
            // would decode every packet, all of them wrong.
            mArmed = false;
            return false;
        }

        channel->AdvanceToAbsPosition( sample );
        ( *out )[ lane ] = ( channel->GetBitState() == BIT_HIGH );
    }
    return true;
}

bool SamplingByteSource::ReadByte( espi::Phase phase, espi::StreamByte* out )
{
    if( out == nullptr )
        return false;

    // One assembler per byte. The framing guarantees a byte boundary here, so
    // starting clean is both correct and cheaper than carrying alignment state
    // across a TAR window that deliberately discards it.
    espi::ByteAssembler assembler( mMode, phase );
    const int clocks = espi::ClocksPerByte( mMode );

    uint64_t first = 0;
    uint64_t last = 0;
    for( int i = 0; i < clocks; ++i )
    {
        uint64_t edge = 0;
        if( !NextSamplingEdge( &edge ) )
            return false;

        espi::LaneBits levels{};
        if( !SampleLanes( phase, edge, &levels ) )
            return false;

        if( i == 0 )
            first = LaunchOf( edge );
        last = edge;

        uint8_t value = 0;
        const bool complete = assembler.Feed( levels, &value );
        if( complete != ( i + 1 == clocks ) )
            return false; // geometry disagreement; never guess a byte

        if( complete )
        {
            out->value = value;
            out->span = espi::ByteSpan{ first, last };
            return true;
        }
    }
    return false;
}

bool SamplingByteSource::TurnAround( espi::ByteSpan* span )
{
    uint64_t first = 0;
    uint64_t last = 0;
    for( int i = 0; i < espi::kTurnAroundClocks; ++i )
    {
        uint64_t edge = 0;
        if( !NextSamplingEdge( &edge ) )
            return false;
        if( i == 0 )
            first = LaunchOf( edge );
        last = edge;
    }

    if( span != nullptr )
        *span = espi::ByteSpan{ first, last };
    return true;
}

bool SamplingByteSource::Active() const
{
    return mArmed;
}

espi::IoMode SamplingByteSource::Mode() const
{
    return mMode;
}

} // namespace espi_saleae
