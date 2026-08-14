#include "espi/LaneCodec.h"

namespace espi
{

size_t PackByte( IoMode mode, Phase phase, uint8_t value, LaneBits* out, size_t out_capacity )
{
    const LaneSet lanes = LanesFor( mode, phase );
    const int width = BitsPerClock( mode );
    const int groups = ClocksPerByte( mode );

    if( out == nullptr || out_capacity < static_cast<size_t>( groups ) )
        return 0;

    for( int g = 0; g < groups; ++g )
    {
        LaneBits clock{ false, false, false, false };
        for( int k = 0; k < lanes.count; ++k )
        {
            const int bit = BitPositionFor( g, k, width );
            clock[ lanes.first + k ] = ( ( value >> bit ) & 1u ) != 0;
        }
        out[ g ] = clock;
    }
    return static_cast<size_t>( groups );
}

ByteAssembler::ByteAssembler( IoMode mode, Phase phase )
    : mMode( mode ), mPhase( phase ), mLanes( LanesFor( mode, phase ) ), mGroupsPerByte( ClocksPerByte( mode ) )
{
}

void ByteAssembler::Reset()
{
    mGroup = 0;
    mAccum = 0;
}

bool ByteAssembler::Feed( const LaneBits& lanes, uint8_t* out )
{
    const int width = BitsPerClock( mMode );

    for( int k = 0; k < mLanes.count; ++k )
    {
        const int bit = BitPositionFor( mGroup, k, width );
        if( lanes[ mLanes.first + k ] )
            mAccum |= static_cast<uint8_t>( 1u << bit );
    }

    if( ++mGroup < mGroupsPerByte )
        return false;

    if( out != nullptr )
        *out = mAccum;
    Reset();
    return true;
}

} // namespace espi
