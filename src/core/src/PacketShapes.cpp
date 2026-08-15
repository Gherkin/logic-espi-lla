#include "espi/PacketShape.h"

#include "Opcodes.h"      // private tables -- reachable only from espi_core (rule R3)
#include "PacketShapes.h"

namespace espi
{
namespace
{

// Opcode encodings come from the opcode table, so an opcode byte lives in
// exactly one place. The shape table names opcodes; it never repeats their
// encodings.
// maybe_unused: opcodes whose shape has not been transcribed yet generate a
// constant nobody references. That is the expected state until stages D and E
// land, not a mistake worth a warning.
#define ESPI_OPCODE_CONST( NAME, ENCODING, MASK, CHANNEL, HAS_C1C0 )                                                               \
    [[maybe_unused]] constexpr uint8_t kOpcode_##NAME = ENCODING;
ESPI_OPCODE_TABLE( ESPI_OPCODE_CONST )
#undef ESPI_OPCODE_CONST

// Element names, unqualified, so a table row reads like the figure it was
// transcribed from.
namespace elements
{
constexpr Element Addr16 = Element::Addr16;
constexpr Element Data32 = Element::Data32;
constexpr Element Status16 = Element::Status16;
constexpr Element VwirePacket = Element::VwirePacket;
} // namespace elements

using namespace elements;

// Overloads rather than an initializer list, because a phase may legitimately
// have no elements at all -- GET_STATUS is opcode then CRC, nothing between --
// and a zero-length array is not valid C++.
constexpr ElementList MakeList()
{
    return ElementList{};
}
constexpr ElementList MakeList( Element a )
{
    return ElementList{ { a }, 1 };
}
constexpr ElementList MakeList( Element a, Element b )
{
    return ElementList{ { a, b }, 2 };
}
// maybe_unused: no shape transcribed so far has three or four elements. The
// peripheral and flash packets in stages D and E do.
[[maybe_unused]] constexpr ElementList MakeList( Element a, Element b, Element c )
{
    return ElementList{ { a, b, c }, 3 };
}
[[maybe_unused]] constexpr ElementList MakeList( Element a, Element b, Element c, Element d )
{
    return ElementList{ { a, b, c, d }, 4 };
}

#define ESPI_CMD( ... ) MakeList( __VA_ARGS__ )
#define ESPI_RSP( ... ) MakeList( __VA_ARGS__ )

struct ShapeEntry
{
    uint8_t opcode;
    PacketShape shape;
};

#define ESPI_SHAPE_ENTRY( NAME, CMD, RSP ) ShapeEntry{ kOpcode_##NAME, PacketShape{ CMD, RSP } },
const ShapeEntry kShapes[] = { ESPI_PACKET_SHAPE_TABLE( ESPI_SHAPE_ENTRY ) };
#undef ESPI_SHAPE_ENTRY

#undef ESPI_CMD
#undef ESPI_RSP

} // namespace

size_t ElementFixedSize( Element element )
{
    switch( element )
    {
    case Element::Addr16:
        return 2;
    case Element::Data32:
        return 4;
    case Element::Status16:
        return 2;
    case Element::VwirePacket:
        return 0; // length is carried in the packet's own count byte
    }
    return 0;
}

const char* ElementName( Element element )
{
    switch( element )
    {
    case Element::Addr16:
        return "Address";
    case Element::Data32:
        return "Data";
    case Element::Status16:
        return "Status";
    case Element::VwirePacket:
        return "Virtual Wire Packet";
    }
    return "Unknown";
}

bool LookupShape( uint8_t opcode, PacketShape* out )
{
    // Exact match only. Every shape transcribed so far belongs to an opcode
    // with an exact encoding; the masked short-cycle opcodes carry cycle-type
    // headers, which are not transcribed yet and must report as a gap rather
    // than fall through to a wrong shape.
    for( const ShapeEntry& e : kShapes )
    {
        if( e.opcode != opcode )
            continue;
        if( out != nullptr )
            *out = e.shape;
        return true;
    }
    return false;
}

} // namespace espi
