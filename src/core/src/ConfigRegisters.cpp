#include "espi/ConfigRegisters.h"

#include "ConfigRegisters.h" // private table -- reachable only from espi_core (rule R3)

#include <cstring>

namespace espi
{
namespace
{

enum class Kind : uint8_t
{
    Fields,
    NoFields,
    Reserved,
};

struct RegisterEntry
{
    uint16_t start;
    uint16_t end;
    const char* name;
    Kind kind;
};

#define ESPI_REGISTER_ENTRY( START, END, NAME, KIND ) RegisterEntry{ START, END, NAME, Kind::KIND },
const RegisterEntry kRegisters[] = { ESPI_CONFIG_REGISTER_TABLE( ESPI_REGISTER_ENTRY ) };
#undef ESPI_REGISTER_ENTRY

// Encoded value tables. The enum name is carried as a string so one flat table
// can hold them all and the header stays readable as a transcription.
struct EnumEntry
{
    const char* group;
    uint32_t value;
    const char* text;
};

#define ESPI_ENUM_ENTRY( GROUP, VALUE, TEXT ) EnumEntry{ #GROUP, VALUE, TEXT },
const EnumEntry kEnums[] = { ESPI_CONFIG_ENUM_TABLE( ESPI_ENUM_ENTRY ) };
#undef ESPI_ENUM_ENTRY

struct FieldEntry
{
    uint16_t offset;
    uint8_t high;
    uint8_t low;
    const char* name;
    const char* group; // "None" when the field is a plain number
    bool zero_based;
    ConfigAccess access;
    ConfigDefault default_kind;
    uint32_t default_value;
};

#define ESPI_FIELD_ENTRY( OFFSET, HIGH, LOW, NAME, ENUM, ZERO_BASED, ACCESS, DEFAULT, VALUE )                                      \
    FieldEntry{ OFFSET,   HIGH,    LOW,          NAME,  #ENUM, ZERO_BASED,                                                         \
                ConfigAccess::ACCESS, ConfigDefault::DEFAULT, VALUE },
const FieldEntry kFields[] = { ESPI_CONFIG_FIELD_TABLE( ESPI_FIELD_ENTRY ) };
#undef ESPI_FIELD_ENTRY

#define ESPI_CHANNEL_ENTRY( BIT, NAME ) ChannelSupportBit{ BIT, NAME },
const ChannelSupportBit kChannels[] = { ESPI_CHANNEL_SUPPORTED_TABLE( ESPI_CHANNEL_ENTRY ) };
#undef ESPI_CHANNEL_ENTRY

#define ESPI_ERASE_BLOCK_ENTRY( BIT, NAME ) TargetEraseBlockBit{ BIT, NAME },
const TargetEraseBlockBit kEraseBlocks[] = { ESPI_TARGET_ERASE_BLOCK_TABLE( ESPI_ERASE_BLOCK_ENTRY ) };
#undef ESPI_ERASE_BLOCK_ENTRY

const char* LookupEnum( const char* group, uint32_t value )
{
    if( std::strcmp( group, "None" ) == 0 )
        return nullptr;
    for( const EnumEntry& e : kEnums )
        if( e.value == value && std::strcmp( e.group, group ) == 0 )
            return e.text;
    return nullptr;
}

uint32_t Extract( uint32_t value, uint8_t high, uint8_t low )
{
    const uint8_t width = static_cast<uint8_t>( high - low + 1 );
    const uint32_t mask = ( width >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << width ) - 1u );
    return ( value >> low ) & mask;
}

} // namespace

ConfigAddress ClassifyConfigAddress( uint16_t address, const char** name )
{
    // Resolve the range FIRST, before judging the address, so that even a
    // malformed one can report which register it was reaching for. "0006h is
    // inside Device Identification, but bits [1:0] are hard-wired to 00" is a
    // more useful thing to tell a person than "names no register".
    //
    // It also makes Table 21's End column observable. Every register range is
    // four bytes and only its base is a legal address, so without this the End
    // of a register range could be transcribed wrongly and nothing would ever
    // read it.
    const uint16_t offset = static_cast<uint16_t>( address & ESPI_CONFIG_ADDRESS_SELECT_MASK );
    const RegisterEntry* entry = nullptr;
    for( const RegisterEntry& r : kRegisters )
    {
        if( offset >= r.start && offset <= r.end )
        {
            entry = &r;
            break;
        }
    }
    if( name != nullptr && entry != nullptr )
        *name = entry->name;

    // Now judge the address itself. Masking it and carrying on would decode
    // the right register and throw away the fact that the controller is out
    // of spec.
    if( ( address & ESPI_CONFIG_ADDRESS_UPPER_MASK ) != 0 )
        return ConfigAddress::UpperBitsSet;
    if( ( address & ESPI_CONFIG_ADDRESS_DWORD_MASK ) != 0 )
        return ConfigAddress::NotDwordAligned;

    // Table 21 covers 000h-FFFh with no holes, so a null entry here means the
    // transcription has one. Reported as reserved rather than asserted,
    // because an analyzer must not crash on a malformed bus.
    if( entry == nullptr )
        return ConfigAddress::ReservedRange;

    switch( entry->kind )
    {
    case Kind::Fields:
        return ConfigAddress::Decoded;
    case Kind::NoFields:
        return ConfigAddress::NoFieldLayout;
    case Kind::Reserved:
        break;
    }
    return ConfigAddress::ReservedRange;
}

bool LookupConfigRegister( uint16_t address, const char** name )
{
    return ClassifyConfigAddress( address, name ) == ConfigAddress::Decoded;
}

size_t DecodeConfigRegister( uint16_t address, uint32_t value, ConfigField* out, size_t capacity )
{
    const uint16_t offset = static_cast<uint16_t>( address & ESPI_CONFIG_ADDRESS_SELECT_MASK );
    size_t count = 0;

    for( const FieldEntry& f : kFields )
    {
        if( f.offset != offset )
            continue;

        if( count < capacity && out != nullptr )
        {
            ConfigField& field = out[ count ];
            field.high = f.high;
            field.low = f.low;
            field.name = f.name;
            field.reserved = ( std::strcmp( f.name, "Reserved" ) == 0 );
            field.zero_based = f.zero_based;
            field.value = Extract( value, f.high, f.low );
            field.meaning = LookupEnum( f.group, field.value );
            field.access = f.access;
            field.default_kind = f.default_kind;
            field.default_value = f.default_value;
        }
        ++count;
    }
    return count;
}

bool ConfigResetValue( uint16_t address, uint32_t* value, uint32_t* known )
{
    const char* name = nullptr;
    if( ClassifyConfigAddress( address, &name ) != ConfigAddress::Decoded )
        return false;

    uint32_t reset = 0;
    uint32_t mask = 0;

    for( const FieldEntry& f : kFields )
    {
        if( f.offset != address )
            continue;
        if( f.default_kind != ConfigDefault::Value )
            continue; // HwInit or blank: a property of the part, not the spec

        const uint8_t width = static_cast<uint8_t>( f.high - f.low + 1 );
        const uint32_t field_mask = ( width >= 32 ) ? 0xFFFFFFFFu : ( ( 1u << width ) - 1u );
        reset |= ( f.default_value & field_mask ) << f.low;
        mask |= field_mask << f.low;
    }

    if( value != nullptr )
        *value = reset;
    if( known != nullptr )
        *known = mask;
    return true;
}

size_t ChannelSupportCount()
{
    return sizeof( kChannels ) / sizeof( kChannels[ 0 ] );
}

const ChannelSupportBit& ChannelSupportAt( size_t index )
{
    return kChannels[ index ];
}

uint8_t ChannelSupportPlatformMask()
{
    return ESPI_CHANNEL_SUPPORTED_PLATFORM_MASK;
}

bool IsChannelSupportedField( uint16_t address, const ConfigField& field )
{
    return ( address & ESPI_CONFIG_ADDRESS_SELECT_MASK ) == 0x008 && field.high == 7 && field.low == 0;
}

size_t TargetEraseBlockCount()
{
    return sizeof( kEraseBlocks ) / sizeof( kEraseBlocks[ 0 ] );
}

const TargetEraseBlockBit& TargetEraseBlockAt( size_t index )
{
    return kEraseBlocks[ index ];
}

uint8_t TargetEraseBlockReservedMask()
{
    return ESPI_TARGET_ERASE_BLOCK_RESERVED_MASK;
}

bool IsTargetEraseBlockField( uint16_t address, const ConfigField& field )
{
    return ( address & ESPI_CONFIG_ADDRESS_SELECT_MASK ) == 0x044 && field.high == 15 && field.low == 8;
}

} // namespace espi
