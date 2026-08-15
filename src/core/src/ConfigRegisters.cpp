#include "espi/ConfigRegisters.h"

#include "ConfigRegisters.h" // private table -- reachable only from espi_core (rule R3)

#include <cstring>

namespace espi
{
namespace
{

struct RegisterEntry
{
    uint16_t offset;
    const char* name;
};

#define ESPI_REGISTER_ENTRY( OFFSET, NAME ) RegisterEntry{ OFFSET, NAME },
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
};

#define ESPI_FIELD_ENTRY( OFFSET, HIGH, LOW, NAME, ENUM, ZERO_BASED )                                                              \
    FieldEntry{ OFFSET, HIGH, LOW, NAME, #ENUM, ZERO_BASED },
const FieldEntry kFields[] = { ESPI_CONFIG_FIELD_TABLE( ESPI_FIELD_ENTRY ) };
#undef ESPI_FIELD_ENTRY

#define ESPI_CHANNEL_ENTRY( BIT, NAME ) ChannelSupportBit{ BIT, NAME },
const ChannelSupportBit kChannels[] = { ESPI_CHANNEL_SUPPORTED_TABLE( ESPI_CHANNEL_ENTRY ) };
#undef ESPI_CHANNEL_ENTRY

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

bool LookupConfigRegister( uint16_t address, const char** name )
{
    const uint16_t offset = static_cast<uint16_t>( address & ESPI_CONFIG_ADDRESS_MASK );
    for( const RegisterEntry& r : kRegisters )
    {
        if( r.offset != offset )
            continue;
        if( name != nullptr )
            *name = r.name;
        return true;
    }
    return false;
}

size_t DecodeConfigRegister( uint16_t address, uint32_t value, ConfigField* out, size_t capacity )
{
    const uint16_t offset = static_cast<uint16_t>( address & ESPI_CONFIG_ADDRESS_MASK );
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
        }
        ++count;
    }
    return count;
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
    return ( address & ESPI_CONFIG_ADDRESS_MASK ) == 0x008 && field.high == 7 && field.low == 0;
}

} // namespace espi
