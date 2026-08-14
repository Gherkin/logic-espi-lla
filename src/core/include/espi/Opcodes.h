#ifndef ESPI_OPCODES_H
#define ESPI_OPCODES_H

#include <cstdint>

namespace espi
{

// Which eSPI channel a command belongs to.
enum class ChannelId : uint8_t
{
    Peripheral,
    VirtualWire,
    Oob,
    Flash,
    ChannelIndependent,
};

const char* ChannelName( ChannelId channel );

struct OpcodeInfo
{
    const char* name = nullptr;
    uint8_t encoding = 0;      // the opcode byte as seen on the bus
    ChannelId channel = ChannelId::ChannelIndependent;
    bool has_short_length = false; // low two bits are C1C0
    uint8_t request_length = 0;    // resolved from C1C0; 0 when not applicable
    bool length_reserved = false;  // C1C0 == 10b, which the spec reserves
};

// Look up an opcode byte. Returns false for an encoding the spec does not
// define -- the decoder reports that rather than guessing.
bool LookupOpcode( uint8_t opcode, OpcodeInfo* out );

// Request length in bytes for a C1C0 encoding. Returns 0 for the reserved
// encoding 10b; callers must treat that as malformed, not as zero-length.
uint8_t ShortRequestLength( uint8_t c1c0 );

} // namespace espi

#endif // ESPI_OPCODES_H
