#include "espi/VirtualWires.h"

#include "VirtualWires.h" // private table -- reachable only from espi_core (rule R3)

#include <cstring>

namespace espi
{
namespace
{

struct RangeEntry
{
    uint8_t start;
    uint8_t end;
    const char* group;
    VwireFormat format;
    VwireDirection direction;
    const char* reset_domain; // empty where the range states none
};

#define ESPI_VWIRE_RANGE_ENTRY( START, END, GROUP, FORMAT, DIRECTION, RESET )                                                       \
    RangeEntry{ START, END, GROUP, VwireFormat::FORMAT, VwireDirection::DIRECTION, RESET },
const RangeEntry kRanges[] = { ESPI_VWIRE_INDEX_TABLE( ESPI_VWIRE_RANGE_ENTRY ) };
#undef ESPI_VWIRE_RANGE_ENTRY

// The table writes an empty string where the specification states no reset
// domain, because a bare nullptr in a transcription row reads as an omission
// rather than a finding.
const char* OrNull( const char* text )
{
    return ( text != nullptr && text[ 0 ] != '\0' ) ? text : nullptr;
}

struct IndexEntry
{
    uint8_t index;
    const char* reset_domain;
    VwireDirection direction;
};

#define ESPI_VWIRE_INDEX_ENTRY( INDEX, RESET, DIRECTION ) IndexEntry{ INDEX, RESET, VwireDirection::DIRECTION },
const IndexEntry kIndices[] = { ESPI_VWIRE_SYSTEM_EVENT_INDEX_TABLE( ESPI_VWIRE_INDEX_ENTRY ) };
#undef ESPI_VWIRE_INDEX_ENTRY

struct WireEntry
{
    uint8_t index;
    uint8_t level_bit;
    const char* name;
    VwirePolarity polarity;
    VwireResetState reset;
};

#define ESPI_VWIRE_WIRE_ENTRY( INDEX, BIT, NAME, POLARITY, RESET )                                                                  \
    WireEntry{ INDEX, BIT, NAME, VwirePolarity::POLARITY, VwireResetState::RESET },
const WireEntry kWires[] = { ESPI_VWIRE_SYSTEM_EVENT_TABLE( ESPI_VWIRE_WIRE_ENTRY ) };
#undef ESPI_VWIRE_WIRE_ENTRY

const IndexEntry* FindIndex( uint8_t index )
{
    for( const IndexEntry& e : kIndices )
        if( e.index == index )
            return &e;
    return nullptr;
}

bool HasWires( uint8_t index )
{
    for( const WireEntry& w : kWires )
        if( w.index == index )
            return true;
    return false;
}

} // namespace

bool LookupVwireIndex( uint8_t index, VwireIndexInfo* out )
{
    for( const RangeEntry& r : kRanges )
    {
        if( index < r.start || index > r.end )
            continue;

        if( out != nullptr )
        {
            out->start = r.start;
            out->end = r.end;
            out->group = r.group;
            out->format = r.format;
            out->direction = r.direction;
            out->reset_domain = OrNull( r.reset_domain );
            out->named_wires = HasWires( index );

            // Tables 9-14 state a reset domain and a direction per index. They
            // sit inside the System Event range, so they refine the range row
            // rather than replacing it.
            if( const IndexEntry* e = FindIndex( index ) )
            {
                out->reset_domain = e->reset_domain;
                out->direction = e->direction;
            }
        }
        return r.format != VwireFormat::NotDefined;
    }

    // Table 8 covers 0-255 with no holes, so reaching here means the
    // transcription has one. Reported rather than asserted: an analyzer must
    // not crash on a malformed bus.
    return false;
}

size_t VwireBitsForIndex( uint8_t index, VwireBit* out, size_t capacity )
{
    size_t count = 0;
    for( const WireEntry& w : kWires )
    {
        if( w.index != index )
            continue;

        if( count < capacity && out != nullptr )
        {
            VwireBit& bit = out[ count ];
            bit.level_bit = w.level_bit;
            bit.valid_bit = VwireValidBitFor( w.level_bit );
            bit.name = w.name;
            bit.polarity = w.polarity;
            bit.reset = w.reset;
            bit.reserved = ( std::strcmp( w.name, "RSV" ) == 0 );
        }
        ++count;
    }
    return count;
}

unsigned VwireGroupCount( uint8_t count_byte )
{
    return static_cast<unsigned>( count_byte & ESPI_VWIRE_COUNT_MASK ) + 1u;
}

uint8_t VwireCountReservedBits( uint8_t count_byte )
{
    return static_cast<uint8_t>( count_byte & ESPI_VWIRE_COUNT_RESERVED_MASK );
}

unsigned VwireMaxGroups()
{
    return ESPI_VWIRE_MAX_GROUPS;
}

uint8_t VwireIrqLevelBit()
{
    return ESPI_VWIRE_IRQ_LEVEL_BIT;
}

unsigned VwireIrqNumber( uint8_t index, uint8_t data )
{
    return static_cast<unsigned>( index ) * ESPI_VWIRE_IRQ_BANK
           + static_cast<unsigned>( data & ESPI_VWIRE_IRQ_LINE_MASK );
}

uint8_t VwireValidBitFor( uint8_t level_bit )
{
    return static_cast<uint8_t>( level_bit + ESPI_VWIRE_VALID_SHIFT );
}

uint8_t VwireLevelMask()
{
    return ESPI_VWIRE_LEVEL_MASK;
}

} // namespace espi
