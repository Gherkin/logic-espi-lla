#ifndef ESPI_VIRTUAL_WIRES_H
#define ESPI_VIRTUAL_WIRES_H

#include <cstddef>
#include <cstdint>

namespace espi
{

// The virtual wire index space and the wires behind each System Event index.
//
// Transcribed in src/core/tables/VirtualWires.h from base spec Table 8,
// pp.60-62 and Tables 9-14, pp.63-69 -- that header is where the values live
// and what the QC worksheet checks.

// How a virtual wire data byte is laid out. Table 8, pp.60-62.
enum class VwireFormat : uint8_t
{
    Interrupt,  // bit 7 is the interrupt level, bits 6:0 the IRQ line
    ValidLevel, // bits 7:4 are valid bits, bits 3:0 the levels they gate
    NotDefined, // the base specification gives the range no data format
};

// Which way the wires in a group travel. Stated per index for the System
// Events and in the table cell for the interrupt range.
//
// Configurable and Unspecified are deliberately different answers, the same way
// a config register with no transcribed layout differs from one that is not a
// register. Unspecified means the specification is silent. Configurable means
// it addresses the question and says the answer varies -- the GPIO Expander
// indices carry input pins or output pins depending on how the platform set
// them up, and that setup never appears on the bus, so no decode can recover
// it. Reporting it as one fixed direction would be an invention.
enum class VwireDirection : uint8_t
{
    ControllerToTarget,
    TargetToController,
    Configurable, // set per index by implementation specific configuration
    Unspecified,  // the specification does not state one for this range
};

// The "Polarity:" line of a wire's description.
enum class VwirePolarity : uint8_t
{
    ActiveHigh,
    ActiveLow,
    AsDefined, // the wire's own text gives '0' and '1' a meaning of their own
    None,      // reserved rows
};

// The "Reset:" line of a wire's description.
enum class VwireResetState : uint8_t
{
    Active,
    Inactive,
    Zero, // the page prints Reset: '0'
    None, // reserved rows
};

struct VwireIndexInfo
{
    uint8_t start = 0;
    uint8_t end = 0;
    const char* group = nullptr; // "System Event", "Platform specific", ...
    VwireFormat format = VwireFormat::NotDefined;
    VwireDirection direction = VwireDirection::Unspecified;

    // The reset domain the group belongs to -- "eSPI Reset#" or "PLTRST#" for
    // the System Event indices, and a note that it is programmable for the
    // GPIO Expander range. Null where the specification states none.
    const char* reset_domain = nullptr;

    // True when the per-bit wire names for this index are transcribed. False
    // for a valid/level index whose wires nobody has read off a page: the
    // format is known, the names are not, and the decoder must say so.
    bool named_wires = false;
};

// Which range of Table 8 an index falls in. Every index 0-255 is in exactly
// one range, so this always succeeds; the return says whether the range has a
// data format the decoder can act on.
bool LookupVwireIndex( uint8_t index, VwireIndexInfo* out );

// One virtual wire of a System Event index.
struct VwireBit
{
    uint8_t level_bit = 0; // 3 down to 0, the order the pages print
    uint8_t valid_bit = 0; // level_bit + 4, Table 8's 1-to-1 rule
    const char* name = nullptr;
    VwirePolarity polarity = VwirePolarity::None;
    VwireResetState reset = VwireResetState::None;
    bool reserved = false; // the page prints RSV in the Virtual Wire column
};

// The four wires of a System Event index, most significant level bit first.
// Returns 0 for an index whose wires are not transcribed, which is every index
// outside 2-7. A return greater than `capacity` means the caller's buffer was
// too small and nothing was written past the end.
size_t VwireBitsForIndex( uint8_t index, VwireBit* out, size_t capacity );

// ---------------------------------------------------------------------------
//  The count byte -- section 4.2.2, p.57. Bits 5:0 are a 0-based count of the
//  virtual wire groups that follow; bits 7:6 are Reserved.
// ---------------------------------------------------------------------------
unsigned VwireGroupCount( uint8_t count_byte );  // 1 through 64
uint8_t VwireCountReservedBits( uint8_t count_byte );
unsigned VwireMaxGroups();

// ---------------------------------------------------------------------------
//  The interrupt event data format -- Table 8, p.60. Only meaningful for an
//  index whose format is VwireFormat::Interrupt.
// ---------------------------------------------------------------------------
uint8_t VwireIrqLevelBit();                            // bit 7
unsigned VwireIrqNumber( uint8_t index, uint8_t data ); // index * 128 + line

// ---------------------------------------------------------------------------
//  The valid/level pairing -- Table 8, p.61. The valid bit for level bit n is
//  n + 4, so a wire is only meaningful when its valid bit is set. A clear
//  valid bit is a mask: the wire keeps whatever value it had, and the level
//  bit on the wire is a stale echo rather than a state.
// ---------------------------------------------------------------------------
uint8_t VwireValidBitFor( uint8_t level_bit );

// Which bits of a data byte are level bits -- bits 3:0. The valid nibble is
// this mask shifted up by the pairing rule rather than a constant of its own.
uint8_t VwireLevelMask();

} // namespace espi

#endif // ESPI_VIRTUAL_WIRES_H
