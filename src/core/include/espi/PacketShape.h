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
    Addr16,      // 2 bytes, MSB first -- the configuration register address
    Data32,      // 4 bytes, LSB first
    Status16,    // 2 bytes, LSB first
    VwirePacket, // count byte, then 2 * (count + 1) bytes of index/data pairs

    // --- peripheral channel, stage D ---
    CycleHeader, // cycle type byte, then the header its layout calls for
    Payload,     // the data bytes the preceding CycleHeader's Length counts

    // The short cycles carry no cycle type, no tag and no length: the opcode
    // is all three (section 3.8, p.39). Their address widths come from
    // Figures 35 and 37, most significant byte first, and are separate
    // elements from Addr16 because nothing here names a config register.
    IoAddr16,  // 2 bytes, MSB first -- short I/O address
    MemAddr32, // 4 bytes, MSB first -- short memory address
    ShortData, // 1, 2 or 4 bytes, the count carried by C1C0 in the opcode
};

// Bytes an element occupies, or 0 when the length is not fixed. Four elements
// are variable: VwirePacket carries its length in its own count byte,
// CycleHeader in its cycle type byte, Payload in the header before it, and
// ShortData in the opcode.
size_t ElementFixedSize( Element element );

const char* ElementName( Element element );

// Whether an element in a RESPONSE phase is present only when the target
// answered ACCEPT. Always false-irrelevant in a command phase, where the
// controller drives every element the shape names.
//
// This is not a stylistic distinction -- it changes how many bytes the
// response phase holds, so getting it wrong puts the CRC on the wrong byte.
// Figures 24 and 25, pp.38-39, show the same PUT_NP answered two ways:
// `ACCEPT HDR DATA STS CRC` when the target completes as connected, and
// `DEFER STS CRC` when it does not. Figure 26, p.40, shows the same split for
// the short reads, whose response carries raw data with no header at all.
bool ElementPresentOnlyOnAccept( Element element );

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
//
// Matching honours the opcode table's mask, so the four short cycles resolve
// for every C1C0 rather than only for a request length of one byte.
bool LookupShape( uint8_t opcode, PacketShape* out );

} // namespace espi

#endif // ESPI_PACKET_SHAPE_H
