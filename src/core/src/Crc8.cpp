#include "espi/Crc8.h"

namespace espi
{

void Crc8::Update( uint8_t byte )
{
    // Bit-at-a-time rather than a lookup table: this runs once per protocol
    // byte, never in a hot loop, and the shift-and-xor form is what a human
    // checks against the spec's Q0..Q7 clock table at QC-1.
    mCrc ^= byte;
    for( int bit = 0; bit < 8; ++bit )
    {
        if( mCrc & 0x80 )
            mCrc = static_cast<uint8_t>( ( mCrc << 1 ) ^ kPolynomial );
        else
            mCrc = static_cast<uint8_t>( mCrc << 1 );
    }
}

void Crc8::Update( const uint8_t* data, size_t length )
{
    for( size_t i = 0; i < length; ++i )
        Update( data[ i ] );
}

uint8_t Crc8::Compute( const uint8_t* data, size_t length )
{
    Crc8 crc;
    crc.Update( data, length );
    return crc.Value();
}

uint8_t Crc8::Compute( std::initializer_list<uint8_t> bytes )
{
    Crc8 crc;
    for( uint8_t b : bytes )
        crc.Update( b );
    return crc.Value();
}

} // namespace espi
