#include "espi/PacketShape.h"

#include "CycleTypes.h"   // private tables -- reachable only from espi_core (rule R3)
#include "Opcodes.h"
#include "PacketShapes.h"

namespace espi
{
namespace
{

// Opcode encodings come from the opcode table, so an opcode byte lives in
// exactly one place. The shape table names opcodes; it never repeats their
// encodings, and it never repeats their masks either -- the four short cycles
// match on 0xFC because Table 2 says their low two bits are C1C0, not because
// this file decided so.
// maybe_unused: opcodes whose shape has not been transcribed yet generate a
// constant nobody references. That is the expected state until stage E lands,
// not a mistake worth a warning.
#define ESPI_OPCODE_CONST( NAME, ENCODING, MASK, CHANNEL, HAS_C1C0 )                                                               \
    [[maybe_unused]] constexpr uint8_t kOpcode_##NAME = ENCODING;                                                                  \
    [[maybe_unused]] constexpr uint8_t kOpcodeMask_##NAME = MASK;
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
constexpr Element CycleHeader = Element::CycleHeader;
constexpr Element Payload = Element::Payload;
constexpr Element IoAddr16 = Element::IoAddr16;
constexpr Element MemAddr32 = Element::MemAddr32;
constexpr Element ShortData = Element::ShortData;
} // namespace elements

// Same reason as `elements`: the framing column is meant to read as one word
// against section 8.3.2, not as a qualified enumerator.
namespace framing
{
constexpr PacketFraming Framed = PacketFraming::Framed;
constexpr PacketFraming NoCrcNoResponse = PacketFraming::NoCrcNoResponse;
} // namespace framing

using namespace elements;
using namespace framing;

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
constexpr ElementList MakeList( Element a, Element b, Element c )
{
    return ElementList{ { a, b, c }, 3 };
}
// maybe_unused: no shape transcribed so far has four elements. The flash
// packets in stage E may.
[[maybe_unused]] constexpr ElementList MakeList( Element a, Element b, Element c, Element d )
{
    return ElementList{ { a, b, c, d }, 4 };
}

#define ESPI_CMD( ... ) MakeList( __VA_ARGS__ )
#define ESPI_RSP( ... ) MakeList( __VA_ARGS__ )

struct ShapeEntry
{
    uint8_t opcode;
    uint8_t mask;
    PacketShape shape;
};

#define ESPI_SHAPE_ENTRY( NAME, CMD, RSP, FRAMING )                                                                                \
    ShapeEntry{ kOpcode_##NAME, kOpcodeMask_##NAME, PacketShape{ CMD, RSP, FRAMING } },
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
    // Taken from the cycle type table rather than repeated here. Two
    // statements of the same width can drift apart, and the one nothing reads
    // is the one that drifts.
    case Element::IoAddr16:
        return ESPI_CYCLE_SHORT_IO_ADDRESS_BYTES;
    case Element::MemAddr32:
        return ESPI_CYCLE_SHORT_MEMORY_ADDRESS_BYTES;

    // Length carried elsewhere: in the packet's own count byte, in its cycle
    // type byte, in the header before it, or in the opcode.
    case Element::VwirePacket:
    case Element::CycleHeader:
    case Element::Payload:
    case Element::ShortData:
        return 0;
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
    case Element::CycleHeader:
        return "Cycle Header";
    case Element::Payload:
        return "Payload";
    case Element::IoAddr16:
    case Element::MemAddr32:
        return "Address";
    case Element::ShortData:
        return "Data";
    }
    return "Unknown";
}

bool ElementPresentOnlyOnAccept( Element element )
{
    switch( element )
    {
    case Element::CycleHeader:
    case Element::Payload:
    case Element::ShortData:
        return true;
    case Element::Addr16:
    case Element::Data32:
    case Element::Status16:
    case Element::VwirePacket:
    case Element::IoAddr16:
    case Element::MemAddr32:
        break;
    }
    return false;
}

unsigned ResetCommandClocks()
{
    return ESPI_RESET_CLOCKS;
}

uint16_t ResetRegisterStart()
{
    return ESPI_RESET_REGISTER_START;
}

uint16_t ResetRegisterEnd()
{
    return ESPI_RESET_REGISTER_END;
}

bool LookupShape( uint8_t opcode, PacketShape* out )
{
    // Masked match, using each opcode's own mask from Table 2. For every
    // opcode with an exact encoding this is an equality test; for the four
    // short cycles it lets C1C0 vary, which is the whole point of the mask --
    // PUT_IORD_SHORT is 40h through 43h and all four are the same packet
    // shape with a different request length.
    for( const ShapeEntry& e : kShapes )
    {
        if( ( opcode & e.mask ) != e.opcode )
            continue;
        if( out != nullptr )
            *out = e.shape;
        return true;
    }
    return false;
}

} // namespace espi
