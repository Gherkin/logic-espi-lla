#ifndef ESPI_CONFIG_REGISTERS_H
#define ESPI_CONFIG_REGISTERS_H

#include <cstddef>
#include <cstdint>

namespace espi
{

// A field within a capabilities and configuration register.
//
// Transcribed in src/core/tables/ConfigRegisters.h from base spec §6.2,
// pp.92-103 -- that header is where the values live and what the QC worksheet
// checks.
struct ConfigField
{
    uint8_t high = 0;             // most significant bit of the field
    uint8_t low = 0;              // least significant bit
    const char* name = nullptr;   //
    bool reserved = false;        // spec requires this span to read as zero
    bool zero_based = false;      // a count where 0 means one
    uint32_t value = 0;           // extracted from the register
    const char* meaning = nullptr; // decoded encoding, or null for a plain number
};

// Look up a register by its configuration address. Only the low 12 bits are
// decoded (§3.7, p.37). Returns false for an address whose layout has not been
// transcribed, which the decoder reports as an explicit gap.
bool LookupConfigRegister( uint16_t address, const char** name );

// Split a register value into its fields, most significant first. Writes up to
// `capacity` entries and returns how many the register has -- a return greater
// than `capacity` means the caller's buffer was too small and nothing was
// written past the end.
size_t DecodeConfigRegister( uint16_t address, uint32_t value, ConfigField* out, size_t capacity );

// The Channel Supported bit field at offset 08h bits 7:0.
struct ChannelSupportBit
{
    uint8_t bit;
    const char* name;
};
size_t ChannelSupportCount();
const ChannelSupportBit& ChannelSupportAt( size_t index );

// Bits 7:4 of Channel Supported are reserved for platform specific channels
// (p.96), so they are unnamed rather than invalid.
uint8_t ChannelSupportPlatformMask();

// True when this field is the Channel Supported bit field, which needs
// per-bit rendering rather than a single value.
bool IsChannelSupportedField( uint16_t address, const ConfigField& field );

} // namespace espi

#endif // ESPI_CONFIG_REGISTERS_H
