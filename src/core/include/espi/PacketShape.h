#ifndef ESPI_PACKET_SHAPE_H
#define ESPI_PACKET_SHAPE_H

#include <cstddef>
#include <cstdint>

namespace espi
{

// A field that can sit between the framing of a phase. Byte order is a
// property of the element, not of the packet, because eSPI mixes the two
// within one packet (section 5.1, p.86).
enum class Element : uint8_t
{
    Addr16,      // 2 bytes, MSB first
    Data32,      // 4 bytes, LSB first
    Status16,    // 2 bytes, LSB first
    VwirePacket, // count byte, then 2 * (count + 1) bytes of index/data pairs
};

// Bytes an element occupies, or 0 when the length is not fixed (VwirePacket,
// whose length is carried in its own first byte).
size_t ElementFixedSize( Element element );

const char* ElementName( Element element );

// The elements of one phase, in wire order. Four is enough for every shape
// transcribed so far; the table is checked against this bound at build time.
struct ElementList
{
    static constexpr size_t kMax = 4;

    Element items[ kMax ]{};
    uint8_t count = 0;
};

struct PacketShape
{
    ElementList command;  // between the opcode and the command CRC
    ElementList response; // between the response byte and the response CRC
};

// Look up the shape for an opcode. Returns false when the shape has not been
// transcribed from the specification -- the decoder reports that as an
// explicit gap and stops, rather than inventing a packet length.
bool LookupShape( uint8_t opcode, PacketShape* out );

} // namespace espi

#endif // ESPI_PACKET_SHAPE_H
