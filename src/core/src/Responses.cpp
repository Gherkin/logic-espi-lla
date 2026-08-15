#include "espi/Responses.h"

#include "Responses.h" // private table -- reachable only from espi_core (rule R3)

namespace espi
{
namespace
{

// Named constants generated from the table, so the switch below and the
// lookup share one source of truth with the transcription.
#define ESPI_RESPONSE_CONST( NAME, CODE ) constexpr uint8_t kCode_##NAME = CODE;
ESPI_RESPONSE_CODE_TABLE( ESPI_RESPONSE_CONST )
#undef ESPI_RESPONSE_CONST

struct Entry
{
    const char* name;
    uint8_t code;
};

#define ESPI_RESPONSE_ENTRY( NAME, CODE ) Entry{ #NAME, CODE },
const Entry kResponses[] = { ESPI_RESPONSE_CODE_TABLE( ESPI_RESPONSE_ENTRY ) };
#undef ESPI_RESPONSE_ENTRY

struct ModifierEntry
{
    uint8_t encoding;
    const char* description;
};

// Keyed on the encoding rather than on position, so the encoding column is
// load bearing and a mutation to it is catchable.
#define ESPI_MODIFIER_ENTRY( ENCODING, DESCRIPTION ) ModifierEntry{ ENCODING, DESCRIPTION },
const ModifierEntry kModifiers[] = { ESPI_RESPONSE_MODIFIER_TABLE( ESPI_MODIFIER_ENTRY ) };
#undef ESPI_MODIFIER_ENTRY

ResponseCode Classify( uint8_t code )
{
    switch( code )
    {
    case kCode_DEFER:
        return ResponseCode::Defer;
    case kCode_NON_FATAL_ERROR:
        return ResponseCode::NonFatalError;
    case kCode_FATAL_ERROR:
        return ResponseCode::FatalError;
    case kCode_WAIT_STATE:
        return ResponseCode::WaitState;
    case kCode_ACCEPT:
    default:
        return ResponseCode::Accept;
    }
}

} // namespace

bool IsWaitState( uint8_t byte )
{
    // NO_RESPONSE shares the low nibble with WAIT_STATE, so the whole-byte
    // encoding has to be excluded first.
    return byte != ESPI_RESPONSE_NO_RESPONSE_BYTE && ( byte & 0x0F ) == kCode_WAIT_STATE;
}

bool LookupResponse( uint8_t byte, ResponseInfo* out )
{
    if( byte == ESPI_RESPONSE_NO_RESPONSE_BYTE )
    {
        if( out != nullptr )
        {
            out->name = ESPI_RESPONSE_NO_RESPONSE_NAME;
            out->code = ResponseCode::NoResponse;
            out->encoding = byte;
            out->modifier = static_cast<uint8_t>( byte >> 6 );
            out->reserved = static_cast<uint8_t>( ( byte >> 4 ) & 0x3 );
        }
        return true;
    }

    const uint8_t code = static_cast<uint8_t>( byte & 0x0F );
    for( const Entry& e : kResponses )
    {
        if( e.code != code )
            continue;

        if( out != nullptr )
        {
            out->name = e.name;
            out->code = Classify( code );
            out->encoding = byte;
            out->modifier = static_cast<uint8_t>( byte >> 6 );
            out->reserved = static_cast<uint8_t>( ( byte >> 4 ) & 0x3 );
        }
        return true;
    }
    return false;
}

const char* ResponseModifierName( uint8_t r1r0 )
{
    const uint8_t encoding = static_cast<uint8_t>( r1r0 & 0x3 );
    for( const ModifierEntry& m : kModifiers )
        if( m.encoding == encoding )
            return m.description;
    return "unknown";
}

} // namespace espi
