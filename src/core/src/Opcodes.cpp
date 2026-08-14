#include "espi/Opcodes.h"

#include "Opcodes.h" // private table -- reachable only from espi_core (rule R3)

namespace espi
{
namespace
{

struct Entry
{
    const char* name;
    uint8_t encoding;
    uint8_t mask;
    ChannelId channel;
    bool has_short_length;
};

#define ESPI_OPCODE_ENTRY( NAME, ENCODING, MASK, CHANNEL, HAS_C1C0 )                                                               \
    Entry{ #NAME, ENCODING, MASK, ChannelId::CHANNEL, HAS_C1C0 },

const Entry kOpcodes[] = { ESPI_OPCODE_TABLE( ESPI_OPCODE_ENTRY ) };

#undef ESPI_OPCODE_ENTRY

} // namespace

const char* ChannelName( ChannelId channel )
{
    switch( channel )
    {
    case ChannelId::Peripheral:
        return "Peripheral";
    case ChannelId::VirtualWire:
        return "Virtual Wire";
    case ChannelId::Oob:
        return "OOB";
    case ChannelId::Flash:
        return "Flash Access";
    case ChannelId::ChannelIndependent:
        return "Channel Independent";
    }
    return "Unknown";
}

uint8_t ShortRequestLength( uint8_t c1c0 )
{
#define ESPI_SHORT_LENGTH_CASE( ENCODING, LENGTH )                                                                                 \
    case ENCODING:                                                                                                                 \
        return LENGTH;

    switch( c1c0 & 0x3 )
    {
        ESPI_SHORT_LENGTH_TABLE( ESPI_SHORT_LENGTH_CASE )
    }
#undef ESPI_SHORT_LENGTH_CASE
    return 0;
}

bool LookupOpcode( uint8_t opcode, OpcodeInfo* out )
{
    for( const Entry& e : kOpcodes )
    {
        if( ( opcode & e.mask ) != e.encoding )
            continue;

        if( out != nullptr )
        {
            out->name = e.name;
            out->encoding = opcode;
            out->channel = e.channel;
            out->has_short_length = e.has_short_length;
            if( e.has_short_length )
            {
                const uint8_t c1c0 = opcode & 0x3;
                out->request_length = ShortRequestLength( c1c0 );
                out->length_reserved = ( c1c0 == 0x2 );
            }
            else
            {
                out->request_length = 0;
                out->length_reserved = false;
            }
        }
        return true;
    }
    return false;
}

} // namespace espi
