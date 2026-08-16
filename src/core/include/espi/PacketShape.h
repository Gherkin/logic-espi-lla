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

// How a transaction is framed around the elements the table lists.
//
// This was an invariant until RESET was transcribed: every command phase ended
// in a CRC, every response phase existed and ended in one. Section 8.3.2, p.122,
// says RESET has neither, so the framing is now a property of the row.
enum class PacketFraming : uint8_t
{
    // opcode, the command elements, CRC -- TAR -- response byte, the response
    // elements, CRC. Every opcode in Table 2 but RESET.
    Framed,

    // opcode, then bits the target ignores, then the chip select deassertion
    // edge. No CRC byte, no turn-around and no response phase at all.
    NoCrcNoResponse,
};

struct PacketShape
{
    ElementList command;  // between the opcode and the command CRC
    ElementList response; // between the response byte and the response CRC
    PacketFraming framing = PacketFraming::Framed;
};

// --- the In-band RESET command, section 8.3.2 pp.122-123 -------------------
//
// These are the only three numbers the RESET transaction carries, and none of
// them is a packet length -- see the header of src/core/tables/PacketShapes.h
// for why the decode reads to the chip select edge instead.

// Serial clocks the controller drives every I/O line high for, Figure 65 p.123.
// CLOCKS, NOT BYTES: two bytes in Single I/O, four in Dual, eight in Quad,
// which is what lets the target recognise the command whatever mode it is in.
unsigned ResetCommandClocks();

// The one configuration register an In-band RESET returns to its default,
// p.123, as an inclusive offset range. Every other register keeps its value.
uint16_t ResetRegisterStart();
uint16_t ResetRegisterEnd();

// Look up the shape for an opcode. Returns false when the shape has not been
// transcribed from the specification -- the decoder reports that as an
// explicit gap and stops, rather than inventing a packet length.
//
// Matching honours the opcode table's mask, so the four short cycles resolve
// for every C1C0 rather than only for a request length of one byte.
bool LookupShape( uint8_t opcode, PacketShape* out );

} // namespace espi

#endif // ESPI_PACKET_SHAPE_H
