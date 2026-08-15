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

// What a configuration address turns out to be.
//
// The three failure cases are deliberately distinct. A target ignores the top
// four address bits (§3.7, p.38), so a controller driving them nonzero still
// reaches the right register -- but it is violating the specification, and
// silently masking the address off would hide that. Likewise bits [1:0] are
// described as hard-wired to 00, so seeing them set is an anomaly worth
// reporting even though the register still resolves.
enum class ConfigAddress : uint8_t
{
    Decoded,         // named in Table 21 and its fields are transcribed
    NoFieldLayout,   // named in Table 21, fields not transcribed yet
    ReservedRange,   // a reserved or platform specific range
    UpperBitsSet,    // bits 15:12 must be driven to zero by the controller
    NotDwordAligned, // bits [1:0] are hard-wired to 00
};

// Classify an address and report which Table 21 range its low 12 bits land in.
// The name is filled in for every outcome, malformed ones included, so a
// decode can say which register a bad address was reaching for.
ConfigAddress ClassifyConfigAddress( uint16_t address, const char** name );

// Convenience: true only when the address is well formed and has a field
// layout, i.e. ClassifyConfigAddress returned Decoded.
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
