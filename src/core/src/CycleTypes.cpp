#include "espi/CycleTypes.h"

#include "CycleTypes.h" // private table -- reachable only from espi_core (rule R3)

namespace espi
{
namespace
{

#define ESPI_CYCLE_TYPE_ENTRY( NAME, ENCODING, MASK, CHANNEL, DIRECTION, COMMAND_TYPE, LAYOUT, VARIABLE )                           \
    CycleTypeInfo{ NAME,      ENCODING,           MASK,     ChannelId::CHANNEL,        CycleDirection::DIRECTION,                   \
                   CycleCommandType::COMMAND_TYPE, CycleLayout::LAYOUT, CycleVariable::VARIABLE },
const CycleTypeInfo kCycleTypes[] = { ESPI_CYCLE_TYPE_TABLE( ESPI_CYCLE_TYPE_ENTRY ) };
#undef ESPI_CYCLE_TYPE_ENTRY

struct LayoutEntry
{
    CycleLayout layout;
    const char* figure;
    uint8_t header_bytes;
    uint8_t address_bytes;
    bool has_message_code;
    bool has_payload;
    CycleLength length;
    CyclePayload payload;
};

#define ESPI_CYCLE_LAYOUT_ENTRY( LAYOUT, FIGURE, HEADER_BYTES, ADDRESS_BYTES, HAS_MESSAGE_CODE, HAS_PAYLOAD, LENGTH, PAYLOAD )      \
    LayoutEntry{ CycleLayout::LAYOUT,   FIGURE, HEADER_BYTES,  ADDRESS_BYTES,  HAS_MESSAGE_CODE, HAS_PAYLOAD,                       \
                 CycleLength::LENGTH, CyclePayload::PAYLOAD },
const LayoutEntry kLayouts[] = { ESPI_CYCLE_HEADER_LAYOUT_TABLE( ESPI_CYCLE_LAYOUT_ENTRY ) };
#undef ESPI_CYCLE_LAYOUT_ENTRY

struct EraseSizeEntry
{
    uint16_t encoding;
    const char* target_attached;
    const char* controller_attached;
};

#define ESPI_ERASE_SIZE_ENTRY( ENCODING, TARGET, CONTROLLER ) EraseSizeEntry{ ENCODING, TARGET, CONTROLLER },
const EraseSizeEntry kEraseSizes[] = { ESPI_FLASH_ERASE_SIZE_TABLE( ESPI_ERASE_SIZE_ENTRY ) };
#undef ESPI_ERASE_SIZE_ENTRY

#define ESPI_SMBUS_COMMAND_ENTRY( CODE, NAME, HEADER_BYTES ) SmbusCommandInfo{ CODE, NAME, HEADER_BYTES },
const SmbusCommandInfo kSmbusCommands[] = { ESPI_OOB_SMBUS_COMMAND_TABLE( ESPI_SMBUS_COMMAND_ENTRY ) };
#undef ESPI_SMBUS_COMMAND_ENTRY

#define ESPI_MCTP_FIELD_ENTRY( BYTE, HIGH, LOW, NAME ) MctpHeaderField{ BYTE, HIGH, LOW, NAME, 0 },
const MctpHeaderField kMctpFields[] = { ESPI_OOB_MCTP_HEADER_TABLE( ESPI_MCTP_FIELD_ENTRY ) };
#undef ESPI_MCTP_FIELD_ENTRY

struct CodedText
{
    uint8_t encoding;
    const char* text;
};

#define ESPI_CODED_TEXT_ENTRY( ENCODING, TEXT ) CodedText{ ENCODING, TEXT },
const CodedText kSplitCompletions[] = { ESPI_CYCLE_SPLIT_COMPLETION_TABLE( ESPI_CODED_TEXT_ENTRY ) };
const CodedText kMessageRouting[] = { ESPI_CYCLE_MESSAGE_ROUTING_TABLE( ESPI_CODED_TEXT_ENTRY ) };
const CodedText kRpmcTargets[] = { ESPI_CYCLE_RPMC_TARGET_TABLE( ESPI_CODED_TEXT_ENTRY ) };
#undef ESPI_CODED_TEXT_ENTRY

const char* FindText( const CodedText* table, size_t count, uint8_t encoding )
{
    for( size_t i = 0; i < count; ++i )
        if( table[ i ].encoding == encoding )
            return table[ i ].text;
    return nullptr;
}

#define ESPI_MESSAGE_CODE_ENTRY( CODE, NAME, CYCLE_TYPE, ROUTING, DIRECTION, FIELDS, DESCRIPTION )                                  \
    MessageCodeInfo{ CODE, NAME, DESCRIPTION, ROUTING, CycleDirection::DIRECTION, FIELDS },
const MessageCodeInfo kMessageCodes[] = { ESPI_MESSAGE_CODE_TABLE( ESPI_MESSAGE_CODE_ENTRY ) };
#undef ESPI_MESSAGE_CODE_ENTRY

struct ScaleEntry
{
    uint8_t encoding;
    uint32_t nanoseconds;
    const char* text;
};

#define ESPI_LTR_SCALE_ENTRY( ENCODING, NANOSECONDS, TEXT ) ScaleEntry{ ENCODING, NANOSECONDS, TEXT },
const ScaleEntry kLtrScales[] = { ESPI_LTR_SCALE_TABLE( ESPI_LTR_SCALE_ENTRY ) };
#undef ESPI_LTR_SCALE_ENTRY

const ScaleEntry* FindScale( uint8_t scale )
{
    for( const ScaleEntry& e : kLtrScales )
        if( e.encoding == scale )
            return &e;
    return nullptr;
}

// Which bit range a row's variable field occupies. Kept beside the extraction
// rather than in the table, because the shift and mask are properties of the
// note that defines the field, and the table already names the note.
struct VariableField
{
    uint8_t shift;
    uint8_t mask;
};

VariableField FieldFor( CycleVariable variable )
{
    switch( variable )
    {
    case CycleVariable::SplitCompletion:
        return { ESPI_CYCLE_SPLIT_COMPLETION_SHIFT, ESPI_CYCLE_SPLIT_COMPLETION_MASK };
    case CycleVariable::MessageRouting:
        return { ESPI_CYCLE_MESSAGE_ROUTING_SHIFT, ESPI_CYCLE_MESSAGE_ROUTING_MASK };
    case CycleVariable::RpmcTarget:
        return { ESPI_CYCLE_RPMC_TARGET_SHIFT, ESPI_CYCLE_RPMC_TARGET_MASK };
    case CycleVariable::None:
        break;
    }
    return { 0, 0 };
}

} // namespace

bool LookupCycleType( ChannelId channel, uint8_t cycle_type, CycleTypeInfo* out )
{
    for( const CycleTypeInfo& e : kCycleTypes )
    {
        if( e.channel != channel )
            continue;
        if( ( cycle_type & e.mask ) != e.encoding )
            continue;
        if( out != nullptr )
            *out = e;
        return true;
    }
    return false;
}

uint8_t CycleVariableValue( const CycleTypeInfo& info, uint8_t cycle_type )
{
    const VariableField field = FieldFor( info.variable );
    return static_cast<uint8_t>( ( cycle_type >> field.shift ) & field.mask );
}

const char* SplitCompletionText( uint8_t p1p0 )
{
    return FindText( kSplitCompletions, sizeof( kSplitCompletions ) / sizeof( kSplitCompletions[ 0 ] ), p1p0 );
}

const char* MessageRoutingText( uint8_t r2r1r0 )
{
    return FindText( kMessageRouting, sizeof( kMessageRouting ) / sizeof( kMessageRouting[ 0 ] ), r2r1r0 );
}

const char* RpmcTargetText( uint8_t r1r0 )
{
    return FindText( kRpmcTargets, sizeof( kRpmcTargets ) / sizeof( kRpmcTargets[ 0 ] ), r1r0 );
}

bool SplitCompletionViolatesNote2( const CycleTypeInfo& info, uint8_t cycle_type )
{
    // Note 2 names one row: Unsuccessful Completion without Data. Two
    // conditions pick it out of Table 5 and neither does so alone.
    //
    //   variable == SplitCompletion  excludes Successful Completion Without
    //                                Data, which carries no P1P0 field at all
    //   layout == CompletionWithoutData  excludes Successful Completion With
    //                                Data, which is free to be a first or a
    //                                middle completion
    //
    // Applying the note to the successful with-data row would turn ordinary
    // split traffic into a wall of errors, which is the more damaging of the
    // two ways to get this wrong.
    //
    // Both channels are in scope. Note 2 is a note on Table 5, and Table 5
    // gives the Unsuccessful Completion Without Data row twice -- once on the
    // peripheral channel and once on the flash channel, with the same encoding
    // and the same footnote marker.
    if( info.variable != CycleVariable::SplitCompletion )
        return false;
    if( info.layout != CycleLayout::CompletionWithoutData && info.layout != CycleLayout::FlashCompletionWithoutData )
        return false;
    return ( CycleVariableValue( info, cycle_type ) & ESPI_CYCLE_SPLIT_P1_MASK ) == 0;
}

bool LookupCycleHeaderLayout( CycleLayout layout, CycleHeaderLayout* out )
{
    for( const LayoutEntry& e : kLayouts )
    {
        if( e.layout != layout )
            continue;
        if( out != nullptr )
        {
            out->figure = e.figure;
            out->header_bytes = e.header_bytes;
            out->address_bytes = e.address_bytes;
            out->has_message_code = e.has_message_code;
            out->has_payload = e.has_payload;
            out->length = e.length;
            out->payload = e.payload;
        }
        return true;
    }
    return false;
}

FlashEraseSize LookupFlashEraseSize( uint16_t length_field )
{
    FlashEraseSize out;
    for( const EraseSizeEntry& e : kEraseSizes )
    {
        if( e.encoding != length_field )
            continue;
        // An empty cell is Table 16 printing Reserved for that scheme, which is
        // a statement rather than a gap, so it comes back as "no size" the same
        // way an encoding the table never lists does.
        if( e.target_attached[ 0 ] != '\0' )
            out.target_attached = e.target_attached;
        if( e.controller_attached[ 0 ] != '\0' )
            out.controller_attached = e.controller_attached;
        break;
    }
    return out;
}

uint8_t CycleTagOf( uint8_t byte1 )
{
    return static_cast<uint8_t>( ( byte1 >> ESPI_CYCLE_TAG_SHIFT ) & ESPI_CYCLE_TAG_MASK );
}

uint16_t CycleLengthOf( uint8_t byte1, uint8_t byte2 )
{
    const uint16_t high = static_cast<uint16_t>( byte1 & ESPI_CYCLE_LENGTH_HIGH_MASK );
    return static_cast<uint16_t>( ( high << 8 ) | byte2 );
}

unsigned CycleLengthBits()
{
    return ESPI_CYCLE_LENGTH_BITS;
}

unsigned CycleResolvedLength( uint16_t length_field )
{
    return length_field == 0 ? ESPI_CYCLE_LENGTH_ZERO_MEANS : static_cast<unsigned>( length_field );
}

unsigned CycleShortIoAddressBytes()
{
    return ESPI_CYCLE_SHORT_IO_ADDRESS_BYTES;
}

unsigned CycleShortMemoryAddressBytes()
{
    return ESPI_CYCLE_SHORT_MEMORY_ADDRESS_BYTES;
}

unsigned SmbusHeaderBytes()
{
    return ESPI_OOB_SMBUS_HEADER_BYTES;
}

uint8_t SmbusAddressBit0Expected()
{
    return ESPI_OOB_SMBUS_ADDRESS_BIT0_EXPECTED;
}

unsigned SmbusAddressBits()
{
    unsigned bits = 0;
    for( unsigned mask = ESPI_OOB_SMBUS_ADDRESS_MASK; mask != 0; mask >>= 1 )
        ++bits;
    return bits;
}

SmbusPacketInfo DecodeSmbusPacket( unsigned length, uint8_t byte3, uint8_t byte4, uint8_t byte5 )
{
    SmbusPacketInfo info;
    info.address = static_cast<uint8_t>( ( byte3 >> ESPI_OOB_SMBUS_ADDRESS_SHIFT ) & ESPI_OOB_SMBUS_ADDRESS_MASK );
    info.address_bit0 = static_cast<uint8_t>( byte3 & ESPI_OOB_SMBUS_ADDRESS_BIT0_MASK );
    info.command = byte4;
    info.byte_count = byte5;

    // Section 4.2.3, p.73. Signed on purpose: a Length shorter than the SMBus
    // header plus the byte count is a real thing a broken initiator can send,
    // and rendering it as a huge unsigned PEC count would hide it.
    info.pec_bytes = static_cast<int>( length ) - static_cast<int>( ESPI_OOB_SMBUS_HEADER_BYTES )
                     - static_cast<int>( byte5 );
    info.consistent = ( info.pec_bytes == 0 || info.pec_bytes == 1 );
    return info;
}

bool LookupSmbusCommand( uint8_t code, SmbusCommandInfo* out )
{
    for( const SmbusCommandInfo& e : kSmbusCommands )
    {
        if( e.code != code )
            continue;
        if( out != nullptr )
            *out = e;
        return true;
    }
    return false;
}

size_t DecodeMctpHeader( const uint8_t bytes[ 5 ], MctpHeaderField* out, size_t capacity )
{
    size_t count = 0;
    for( const MctpHeaderField& f : kMctpFields )
    {
        if( count < capacity && out != nullptr )
        {
            MctpHeaderField& field = out[ count ];
            field = f;
            const uint8_t width = static_cast<uint8_t>( f.high - f.low + 1 );
            const uint32_t mask = ( 1u << width ) - 1u;
            field.value = ( static_cast<uint32_t>( bytes[ f.byte ] ) >> f.low ) & mask;
        }
        ++count;
    }
    return count;
}

uint8_t MctpSourceBit0( uint8_t byte0 )
{
    return static_cast<uint8_t>( byte0 & ESPI_OOB_MCTP_SOURCE_BIT0_MASK );
}

uint8_t MctpSourceBit0Expected()
{
    return ESPI_OOB_MCTP_SOURCE_BIT0_EXPECTED;
}

bool LookupMessageCode( uint8_t code, MessageCodeInfo* out )
{
    for( const MessageCodeInfo& e : kMessageCodes )
    {
        if( e.code != code )
            continue;
        if( out != nullptr )
            *out = e;
        return true;
    }
    return false;
}

LtrMessage DecodeLtrMessage( uint8_t byte4, uint8_t byte5 )
{
    LtrMessage m;
    m.requirement = ( ( byte4 >> ESPI_LTR_RQ_BIT ) & 1u ) != 0;
    m.reserved = static_cast<uint8_t>( ( byte4 >> ESPI_LTR_RSV_SHIFT ) & ESPI_LTR_RSV_MASK );
    m.scale = static_cast<uint8_t>( ( byte4 >> ESPI_LTR_SCALE_SHIFT ) & ESPI_LTR_SCALE_MASK );
    const uint16_t high = static_cast<uint16_t>( byte4 & ESPI_LTR_VALUE_HIGH_MASK );
    m.value = static_cast<uint16_t>( ( high << 8 ) | byte5 );
    return m;
}

bool LtrScaleNanoseconds( uint8_t scale, uint32_t* out )
{
    const ScaleEntry* e = FindScale( scale );
    if( e == nullptr )
        return false;
    if( out != nullptr )
        *out = e->nanoseconds;
    return true;
}

const char* LtrScaleText( uint8_t scale )
{
    const ScaleEntry* e = FindScale( scale );
    return e != nullptr ? e->text : nullptr;
}

const char* CycleCommandTypeName( CycleCommandType type )
{
    switch( type )
    {
    case CycleCommandType::Posted:
        return "Posted";
    case CycleCommandType::NonPosted:
        return "Non-Posted";
    case CycleCommandType::Completion:
        return "Completion";
    }
    return "Unknown";
}

const char* CycleDirectionText( CycleDirection direction )
{
    // Section 4.1.1, p.46, spelled out rather than reprinted as "Up" and
    // "Down" -- the page defines the two words in a sentence nobody reading a
    // decode has in front of them.
    switch( direction )
    {
    case CycleDirection::Up:
        return "target to controller";
    case CycleDirection::Down:
        return "controller to target";
    case CycleDirection::UpOrDown:
        return "either direction";
    }
    return "unknown direction";
}

size_t CycleTypeCount()
{
    return sizeof( kCycleTypes ) / sizeof( kCycleTypes[ 0 ] );
}

const CycleTypeInfo& CycleTypeAt( size_t index )
{
    return kCycleTypes[ index < CycleTypeCount() ? index : 0 ];
}

} // namespace espi
