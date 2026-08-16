#ifndef ESPI_CONFIG_REGISTERS_H
#define ESPI_CONFIG_REGISTERS_H

#include "espi/IoMode.h"

#include <cstddef>
#include <cstdint>

namespace espi
{

// A field within a capabilities and configuration register.
//
// Transcribed in src/core/tables/ConfigRegisters.h from base spec §6.2,
// pp.92-103 -- that header is where the values live and what the QC worksheet
// checks.
// How software may touch a field.
enum class ConfigAccess : uint8_t
{
    RO,     // read only
    RW,     // read/write
    RwOrRo, // the page prints "RW / RO" -- read only when the target supports
            // only one of the two options the field selects between
};

// What the specification's Default column says.
enum class ConfigDefault : uint8_t
{
    Value,  // a number the spec states, in ConfigField::default_value
    HwInit, // hardware initialised; no value the specification can give
    None,   // the Default column is empty on the page
};

struct ConfigField
{
    uint8_t high = 0;             // most significant bit of the field
    uint8_t low = 0;              // least significant bit
    const char* name = nullptr;   //
    bool reserved = false;        // spec requires this span to read as zero
    bool zero_based = false;      // a count where 0 means one
    uint32_t value = 0;           // extracted from the register
    const char* meaning = nullptr; // decoded encoding, or null for a plain number

    ConfigAccess access = ConfigAccess::RO;
    ConfigDefault default_kind = ConfigDefault::None;
    uint32_t default_value = 0; // meaningful only when default_kind is Value
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

// ---------------------------------------------------------------------------
//  RESET STATE
//
//  Where the state machine starts. A capture begins with the link coming out
//  of eSPI Reset#, and nothing on the wire announces what mode the bus is in
//  -- the very first transaction has to be decoded using the reset defaults or
//  not at all. Single I/O, CRC checking off, peripheral channel enabled and
//  the rest disabled all come from here.
//
//  Writes `value` with every field whose default the specification states, and
//  `known` with a mask of the bits that value covers. Bits outside `known` are
//  hardware initialised or left blank by the spec, so their reset state is a
//  property of the part and cannot be assumed. Returns false for an address
//  with no transcribed layout.
// ---------------------------------------------------------------------------
bool ConfigResetValue( uint16_t address, uint32_t* value, uint32_t* known );

// ---------------------------------------------------------------------------
//  THE TWO FIELDS OF OFFSET 08h THAT CHANGE HOW THE BUS IS READ
//
//  Every other register in the map describes what a channel will carry. These
//  two describe how the wire itself is to be sampled and checked, so they are
//  the only fields the analyzer has to act on rather than merely name.
//
//  Both take effect at the deassertion edge of the Chip Select# that carried
//  them -- §5.1 p.86 for the I/O mode, §5.2 p.90 for CRC checking, and §6.2
//  pp.92-93 for register writes in general. See espi/Session.h for what is
//  done with them.
// ---------------------------------------------------------------------------
struct GeneralConfig
{
    // I/O Mode Select, bits 27:26. Meaningless when mode_reserved is set: the
    // page defines no mode for 11b, so a caller must leave the session where
    // it is rather than read this field.
    IoMode mode = IoMode::Single;
    uint8_t mode_encoding = 0;  // the two bits as written, so 11b survives
    bool mode_reserved = false; // p.95 prints encoding 11b as Reserved
    bool crc_checking = false;  // CRC Checking Enable, bit 31
};

// True when this address names General Capabilities and Configurations. A
// SET_CONFIGURATION to any other register changes nothing about how later
// transactions are decoded.
bool IsGeneralConfigAddress( uint16_t address );

// Split a General Capabilities and Configurations DWord into the two settings
// above. Returns false for any other address, and false if the field layout
// no longer names them -- the decoder then reports nothing rather than a mode
// it guessed.
bool DecodeGeneralConfig( uint16_t address, uint32_t value, GeneralConfig* out );

// The same two settings as they stand out of eSPI Reset# -- Single I/O with
// CRC checking off. Assembled from the Default column above rather than stated
// again here, so a wrong Default is one failure and not two.
//
// It is where every capture that begins at reset begins, and where an In-band
// RESET puts the link back to: §8.3.2, p.123, names 008h-00Bh as the one
// register it restores.
bool GeneralConfigResetState( GeneralConfig* out );

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

// The Target Flash Erase Block Size bit field at offset 44h bits 15:8. A
// capability mask, not an encoded value -- "If multiple bits are set then the
// controller is allowed to issue an erase using any of the indicated sizes"
// (p.105) -- and easy to confuse with the encoded Flash Block Erase Size field
// at 40h bits 4:2, which it is not.
struct TargetEraseBlockBit
{
    uint8_t bit; // numbered within the field, so bit 0 is register bit 8
    const char* name;
};
size_t TargetEraseBlockCount();
const TargetEraseBlockBit& TargetEraseBlockAt( size_t index );

// Bits 0, 1, 3 and 4 of the field, which p.105 prints as Reserved.
uint8_t TargetEraseBlockReservedMask();

bool IsTargetEraseBlockField( uint16_t address, const ConfigField& field );

} // namespace espi

#endif // ESPI_CONFIG_REGISTERS_H
