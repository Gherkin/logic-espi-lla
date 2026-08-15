#include "espi/Status.h"

#include "Status.h" // private table -- reachable only from espi_core (rule R3)

namespace espi
{
namespace
{

#define ESPI_STATUS_ENTRY( NAME, BIT ) StatusBitInfo{ #NAME, BIT },
const StatusBitInfo kStatusBits[] = { ESPI_STATUS_BIT_TABLE( ESPI_STATUS_ENTRY ) };
#undef ESPI_STATUS_ENTRY

} // namespace

size_t StatusBitCount()
{
    return sizeof( kStatusBits ) / sizeof( kStatusBits[ 0 ] );
}

const StatusBitInfo& StatusBitAt( size_t index )
{
    return kStatusBits[ index ];
}

uint16_t StatusReservedMask()
{
    return ESPI_STATUS_RESERVED_MASK;
}

} // namespace espi
