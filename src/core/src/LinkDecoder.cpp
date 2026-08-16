#include "espi/LinkDecoder.h"

#include "espi/ConfigRegisters.h"
#include "espi/Crc8.h"
#include "espi/CycleTypes.h"
#include "espi/Opcodes.h"
#include "espi/PacketShape.h"
#include "espi/Responses.h"
#include "espi/Session.h"
#include "espi/Status.h"
#include "espi/VirtualWires.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace espi
{
namespace
{

std::string Hex( uint64_t value, int digits )
{
    char buf[ 32 ];
    std::snprintf( buf, sizeof( buf ), "0x%0*llX", digits, static_cast<unsigned long long>( value ) );
    return buf;
}

std::string Plural( unsigned count, const char* noun )
{
    char buf[ 64 ];
    std::snprintf( buf, sizeof( buf ), "%u %s%s", count, noun, count == 1 ? "" : "s" );
    return buf;
}

// A child that explains its parent rather than carrying a value of its own.
// Its raw is 0 because there is nothing to put there, and FieldKind::Note is
// what stops a reader treating that 0 as a decoded state -- see Decode.h.
Field Note( const char* name, std::string text, ByteSpan span )
{
    Field field( name, std::move( text ), 0, 8, span );
    field.kind = FieldKind::Note;
    return field;
}

std::string BitText( unsigned bit, unsigned value )
{
    char buf[ 32 ];
    std::snprintf( buf, sizeof( buf ), "bit %u = %u", bit, value );
    return buf;
}

// Reads bytes for one phase, accumulating the CRC and the sample span as it
// goes. WAIT_STATE bytes are read through ReadUncovered so they stay out of
// the CRC -- section 3.10, p.45.
class PhaseReader
{
  public:
    PhaseReader( ByteSource* source, Phase phase ) : mSource( source ), mPhase( phase ) {}

    bool Read( uint8_t* value, ByteSpan* span = nullptr )
    {
        if( !ReadUncovered( value, span ) )
            return false;
        mCrc.Update( *value );
        return true;
    }

    bool ReadUncovered( uint8_t* value, ByteSpan* span = nullptr )
    {
        StreamByte byte;
        if( !mSource->ReadByte( mPhase, &byte ) )
        {
            mTruncated = true;
            return false;
        }
        *value = byte.value;
        if( span != nullptr )
            *span = byte.span;
        mSpan = mHaveSpan ? Merge( mSpan, byte.span ) : byte.span;
        mHaveSpan = true;
        return true;
    }

    // Read `count` bytes into a value, honouring the element's byte order.
    bool ReadInteger( size_t count, bool msb_first, uint64_t* value, ByteSpan* span )
    {
        uint64_t acc = 0;
        ByteSpan total{};
        for( size_t i = 0; i < count; ++i )
        {
            uint8_t byte = 0;
            ByteSpan one{};
            if( !Read( &byte, &one ) )
                return false;
            if( msb_first )
                acc = ( acc << 8 ) | byte;
            else
                acc |= static_cast<uint64_t>( byte ) << ( 8 * i );
            total = ( i == 0 ) ? one : Merge( total, one );
        }
        *value = acc;
        if( span != nullptr )
            *span = total;
        return true;
    }

    // Fold a byte that was read with ReadUncovered into the CRC after the
    // fact. The response byte needs this: it has to be inspected for
    // WAIT_STATE before it can be counted, but it *is* covered by the CRC.
    void CrcUpdate( uint8_t byte ) { mCrc.Update( byte ); }

    uint8_t Crc() const { return mCrc.Value(); }
    ByteSpan Span() const { return mSpan; }
    bool Truncated() const { return mTruncated; }
    espi::Phase PhaseOf() const { return mPhase; }

  private:
    ByteSource* mSource;
    Phase mPhase;
    Crc8 mCrc;
    ByteSpan mSpan{};
    bool mHaveSpan = false;
    bool mTruncated = false;
};

Field MakeField( std::string name, std::string text, uint64_t raw, uint8_t width, ByteSpan span )
{
    return Field( std::move( name ), std::move( text ), raw, width, span );
}

Field ErrorField( std::string name, std::string text )
{
    Field f( std::move( name ), std::move( text ), 0, 8, ByteSpan{} );
    f.severity = Severity::Error;
    return f;
}

// Compare the CRC byte just read against what the phase accumulated. The
// accumulator must be sampled BEFORE the CRC byte itself is folded in, so the
// caller passes the expected value.
Field CrcField( uint8_t received, uint8_t computed, ByteSpan span )
{
    const bool ok = ( received == computed );
    Field f( "CRC", Hex( received, 2 ) + ( ok ? "  ok" : "  BAD, computed " + Hex( computed, 2 ) ), received, 8, span );
    if( !ok )
        f.severity = Severity::Error;
    return f;
}

void AddStatusChildren( Field* status, uint16_t value )
{
    for( size_t i = 0; i < StatusBitCount(); ++i )
    {
        const StatusBitInfo& info = StatusBitAt( i );
        if( ( value >> info.bit ) & 1u )
        {
            char text[ 32 ];
            std::snprintf( text, sizeof( text ), "bit %u = 1", static_cast<unsigned>( info.bit ) );
            status->Add( Field( info.name, text, 1, 1, status->span ) );
        }
    }

    const uint16_t reserved = static_cast<uint16_t>( value & StatusReservedMask() );
    if( reserved != 0 )
    {
        Field f( "Reserved", Hex( reserved, 4 ) + "  must be driven to 0", reserved, 16, status->span );
        f.severity = Severity::Warning;
        status->Add( std::move( f ) );
    }
}

// What one transaction's elements need to know about each other.
//
// Two facts cross a phase boundary and one crosses the turn-around.
// GET_CONFIGURATION puts the register address in the command phase and the
// data in the response, so the address has to survive TAR. A cycle header
// establishes how many payload bytes follow it, and the payload is a separate
// element. The short cycles take their data length from the opcode, which was
// read before either phase's elements.
struct PacketContext
{
    // GET_CONFIGURATION / SET_CONFIGURATION
    bool have_address = false;
    uint16_t address = 0;

    // Which channel's cycle type table applies. From the opcode (section 4.2,
    // p.52: peripheral is channel 0, virtual wire 1, OOB 2, flash 3), because
    // Table 5 note 3 says the byte alone does not identify a cycle type.
    ChannelId channel = ChannelId::ChannelIndependent;

    // C1C0 from a short opcode, resolved to bytes. Zero when the opcode is not
    // a short cycle.
    unsigned short_length = 0;

    // Set by the CycleHeader element for the Payload element that follows it.
    unsigned payload_bytes = 0;
    bool have_cycle_header = false;
    CyclePayload payload_kind = CyclePayload::Opaque;

    // Whether the command phase carried a Posted cycle type. Read in the
    // response phase, because section 3.9, p.42, makes one response code
    // illegal for those: "DEFER response for posted transaction is invalid."
    bool command_is_posted = false;

    // The decode stopped on purpose, because something on the wire left the
    // packet length unknown -- not because the source ran out. The two look
    // identical to the caller of ReadElements and mean opposite things: one is
    // a chip select deasserting mid-packet, the other is a gap in what has
    // been transcribed, and reporting the second as the first would blame the
    // bus for the analyzer's own limits.
    bool stopped = false;

    // --- what this transaction does to the session, Decode.h SessionUpdate ---

    // The command phase wrote a DWord to General Capabilities and
    // Configurations. Recognised by SHAPE rather than by opcode: an Addr16
    // followed by a Data32 in the *command* phase is a configuration write,
    // and GET_CONFIGURATION is the same two elements with the Data32 in the
    // response phase instead. Nothing here has to know 22h.
    bool general_config_write = false;
    GeneralConfig general_config{};

    // The command was an In-band RESET, set where the reset is read rather
    // than by inspecting the opcode a second time.
    bool in_band_reset = false;

    // The response byte resolved to ACCEPT. §5.2, p.90, makes this the
    // condition on a configuration write taking effect -- "upon the successful
    // SET_CONFIGURATION" -- and §8.3.2, p.122, says what the alternative
    // leaves behind.
    bool accepted = false;
};

void AddChannelSupportChildren( Field* parent, uint32_t value )
{
    for( size_t i = 0; i < ChannelSupportCount(); ++i )
    {
        const ChannelSupportBit& c = ChannelSupportAt( i );
        if( ( value >> c.bit ) & 1u )
        {
            char text[ 32 ];
            std::snprintf( text, sizeof( text ), "bit %u = 1", static_cast<unsigned>( c.bit ) );
            parent->Add( Field( c.name, text, 1, 1, parent->span ) );
        }
    }

    const uint32_t platform = value & ChannelSupportPlatformMask();
    if( platform != 0 )
        parent->Add( Field( "Platform specific channels", Hex( platform, 2 ), platform, 8, parent->span ) );
}

// Offset 44h bits 15:8, p.105. A capability mask: every set bit is a size the
// controller may erase with, so it is rendered a bit at a time rather than as a
// number. The neighbouring Flash Block Erase Size field at 40h is an encoded
// value with almost the same name, and reading either as the other is the
// mistake this per-bit rendering makes visible.
void AddTargetEraseBlockChildren( Field* parent, uint32_t value )
{
    for( size_t i = 0; i < TargetEraseBlockCount(); ++i )
    {
        const TargetEraseBlockBit& b = TargetEraseBlockAt( i );
        if( ( value >> b.bit ) & 1u )
            parent->Add( Field( b.name, BitText( b.bit, 1 ) + "  supported", 1, 1, parent->span ) );
    }

    const uint32_t reserved = value & TargetEraseBlockReservedMask();
    if( reserved != 0 )
    {
        Field f( "Reserved", Hex( reserved, 2 ) + "  field bits 0, 1, 3 and 4 must be driven to 0", reserved, 8, parent->span );
        f.severity = Severity::Warning;
        parent->Add( std::move( f ) );
    }
}

// Describe a configuration address: the register it names, or why it does not
// name one. Returns the text to append to the Address field, and sets a
// severity when the address itself is out of spec.
std::string DescribeConfigAddress( uint16_t address, Severity* severity )
{
    const char* name = nullptr;
    *severity = Severity::Info;

    switch( ClassifyConfigAddress( address, &name ) )
    {
    case ConfigAddress::Decoded:
    case ConfigAddress::NoFieldLayout:
    case ConfigAddress::ReservedRange:
        return name != nullptr ? std::string( "  " ) + name : std::string();

    case ConfigAddress::UpperBitsSet:
        // The target ignores these bits, so the transaction still works. That
        // is exactly why it is worth saying: nothing else on the bus will.
        *severity = Severity::Warning;
        return std::string( "  reaches " ) + ( name != nullptr ? name : "no register" )
               + ", but the top 4 address bits must be driven to zero";

    case ConfigAddress::NotDwordAligned:
        *severity = Severity::Warning;
        return std::string( "  inside " ) + ( name != nullptr ? name : "no register" )
               + ", but address bits [1:0] are hard-wired to 00";
    }
    return std::string();
}

// Resolve a configuration DWord into named fields.
void AddConfigChildren( Field* data, uint16_t address, uint32_t value )
{
    const char* register_name = nullptr;
    const ConfigAddress kind = ClassifyConfigAddress( address, &register_name );

    if( kind != ConfigAddress::Decoded )
    {
        std::string why;
        switch( kind )
        {
        case ConfigAddress::NoFieldLayout:
            why = std::string( register_name ) + " -- bit layout not transcribed yet";
            break;
        case ConfigAddress::ReservedRange:
            why = std::string( register_name != nullptr ? register_name : "unmapped" ) + " -- not a register with a layout";
            break;
        case ConfigAddress::UpperBitsSet:
            why = "address is malformed, so the register it names is not trusted";
            break;
        case ConfigAddress::NotDwordAligned:
            why = "address is not DWord aligned, so it names no register";
            break;
        case ConfigAddress::Decoded:
            break;
        }
        Field gap( "Register Layout", why, address, 16, data->span );
        gap.severity = Severity::Warning;
        data->Add( std::move( gap ) );
        return;
    }

    ConfigField fields[ 16 ];
    const size_t count = DecodeConfigRegister( address, value, fields, 16 );
    for( size_t i = 0; i < count && i < 16; ++i )
    {
        const ConfigField& f = fields[ i ];

        if( f.reserved )
        {
            // The specification requires reserved spans to read as zero, so a
            // nonzero one is worth saying out loud and a zero one is noise.
            if( f.value != 0 )
            {
                Field r( "Reserved", Hex( f.value, 1 ) + "  must be driven to 0", f.value, 32, data->span );
                r.severity = Severity::Warning;
                data->Add( std::move( r ) );
            }
            continue;
        }

        std::string text;
        if( f.meaning != nullptr )
            text = Hex( f.value, 1 ) + "  " + f.meaning;
        else if( f.zero_based )
            text = std::to_string( f.value ) + "  resolves to " + std::to_string( f.value + 1 );
        else if( f.high == f.low )
            text = std::to_string( f.value );
        else
            text = Hex( f.value, 2 );

        Field field( f.name, text, f.value, static_cast<uint8_t>( f.high - f.low + 1 ), data->span );
        if( IsChannelSupportedField( address, f ) )
            AddChannelSupportChildren( &field, f.value );
        else if( IsTargetEraseBlockField( address, f ) )
            AddTargetEraseBlockChildren( &field, f.value );
        data->Add( std::move( field ) );
    }
}

// --- virtual wires -------------------------------------------------------

const char* DirectionText( VwireDirection direction )
{
    switch( direction )
    {
    case VwireDirection::ControllerToTarget:
        return "controller to target";
    case VwireDirection::TargetToController:
        return "target to controller";
    case VwireDirection::Configurable:
        // The pins are on the target either way (section 4.2.2.5, p.72); which
        // way the messages travel depends on whether this index was set up as
        // inputs or outputs, and that never appears on the bus.
        return "direction configured per index";
    case VwireDirection::Unspecified:
        break;
    }
    return nullptr;
}

// A level bit read against the wire's polarity. This is the whole point of
// carrying the Polarity column: SLP_S3# at level '0' is S3 sleep being
// requested, and rendering that as "low" leaves the reader to invert it by
// hand against the page the decoder is supposed to replace.
//
// TARGET_BOOT_LOAD_STATUS is the one wire with no assert/release sense -- its
// cell says "Polarity: As defined above" because '0' and '1' mean a corrupted
// and an intact boot image. It gets the level and no claim about assertion.
std::string LevelText( unsigned level, VwirePolarity polarity )
{
    const char* state = level != 0 ? "high" : "low";
    switch( polarity )
    {
    case VwirePolarity::ActiveHigh:
        return std::string( state ) + ( level != 0 ? ", asserted" : ", deasserted" );
    case VwirePolarity::ActiveLow:
        return std::string( state ) + ( level == 0 ? ", asserted" : ", deasserted" );
    case VwirePolarity::AsDefined:
    case VwirePolarity::None:
        break;
    }
    return state;
}

// Interrupt event, Table 8 p.60: bit 7 is the level, bits 6:0 the IRQ line,
// and the index picks the bank of 128 the line falls in.
void AddInterruptChildren( Field* data, uint8_t index, uint8_t value )
{
    const unsigned level = ( value >> VwireIrqLevelBit() ) & 1u;
    data->Add( Field( "Interrupt Level", BitText( VwireIrqLevelBit(), level ) + ( level != 0 ? "  asserted" : "  deasserted" ),
                      level, 1, data->span ) );

    const unsigned irq = VwireIrqNumber( index, value );
    data->Add( Field( "Interrupt Line", Hex( value & 0x7Fu, 2 ) + "  IRQ " + std::to_string( irq ), value & 0x7Fu, 7,
                      data->span ) );
}

// A valid/level byte whose wire names are transcribed -- the System Event
// indices, Tables 9-14.
void AddNamedWireChildren( Field* data, uint8_t index, uint8_t value )
{
    VwireBit wires[ 8 ];
    const size_t count = VwireBitsForIndex( index, wires, 8 );

    uint8_t reserved_mask = 0;
    std::string masked;

    for( size_t i = 0; i < count && i < 8; ++i )
    {
        const VwireBit& w = wires[ i ];
        const unsigned level = ( value >> w.level_bit ) & 1u;
        const bool valid = ( ( value >> w.valid_bit ) & 1u ) != 0;

        if( w.reserved )
        {
            reserved_mask |= static_cast<uint8_t>( ( 1u << w.level_bit ) | ( 1u << w.valid_bit ) );
            continue;
        }

        if( !valid )
        {
            // A clear valid bit is a mask, not an absence: the wire keeps its
            // previous value and the level bit here is a stale echo. Naming
            // the wire without its level is the honest rendering.
            if( !masked.empty() )
                masked += ", ";
            masked += w.name;
            continue;
        }

        data->Add( Field( w.name, BitText( w.level_bit, level ) + "  " + LevelText( level, w.polarity ), level, 1, data->span ) );
    }

    if( !masked.empty() )
        data->Add( Note( "Masked", masked + "  valid bit clear, level not updated", data->span ) );

    const uint8_t reserved = static_cast<uint8_t>( value & reserved_mask );
    if( reserved != 0 )
    {
        Field f( "Reserved", Hex( reserved, 2 ) + "  must be driven to 0", reserved, 8, data->span );
        f.severity = Severity::Warning;
        data->Add( std::move( f ) );
    }
}

// A valid/level byte in a range that has the format but no transcribed names
// -- the General Purpose I/O Expander indices. The bits are resolved as far as
// the specification goes and no further: it says only that each level bit is
// "the state of a virtual GPIO to be communicated", never which GPIO.
void AddUnnamedWireChildren( Field* data, uint8_t value )
{
    std::string masked;
    const uint8_t level_mask = VwireLevelMask();
    for( int bit = 7; bit >= 0; --bit )
    {
        const unsigned level_bit = static_cast<unsigned>( bit );
        if( ( ( level_mask >> level_bit ) & 1u ) == 0 )
            continue;

        const unsigned level = ( value >> level_bit ) & 1u;
        if( ( ( value >> VwireValidBitFor( static_cast<uint8_t>( level_bit ) ) ) & 1u ) == 0 )
        {
            if( !masked.empty() )
                masked += ", ";
            masked += "bit " + std::to_string( level_bit );
            continue;
        }
        data->Add( Field( "Level[" + std::to_string( level_bit ) + "]", BitText( level_bit, level ) + "  " + LevelText( level, VwirePolarity::None ),
                          level, 1, data->span ) );
    }

    if( !masked.empty() )
        data->Add( Note( "Masked", masked + "  valid bit clear, level not updated", data->span ) );
}

// Resolve one index/data pair into named wires, or say why it cannot be.
void AddVwireGroup( Field* packet, uint8_t index, ByteSpan index_span, uint8_t data, ByteSpan data_span )
{
    VwireIndexInfo info;
    const bool has_format = LookupVwireIndex( index, &info );

    std::string index_text = Hex( index, 2 );
    if( info.group != nullptr )
    {
        index_text += std::string( "  " ) + info.group;
        if( const char* direction = DirectionText( info.direction ) )
            index_text += std::string( ", " ) + direction;
    }

    Field index_field( "Index", index_text, index, 8, index_span );
    // 8h-3Fh is Reserved in Table 8, so an index in that range should not be
    // on the bus at all. 40h-7Fh is platform specific, which is legal traffic
    // this document simply does not define -- a gap, not a fault.
    if( info.group != nullptr && std::string( info.group ) == "Reserved" )
        index_field.severity = Severity::Warning;
    packet->Add( std::move( index_field ) );

    Field data_field( "Data", Hex( data, 2 ), data, 8, data_span );

    if( !has_format )
    {
        Field gap( "Virtual Wire Layout",
                   info.group != nullptr
                       ? std::string( info.group ) + " -- the base specification defines no wires for this index"
                       : std::string( "index falls outside every range of Table 8" ),
                   index, 8, data_span );
        gap.severity = Severity::Warning;
        data_field.Add( std::move( gap ) );
    }
    else if( info.format == VwireFormat::Interrupt )
    {
        AddInterruptChildren( &data_field, index, data );
    }
    else if( info.named_wires )
    {
        AddNamedWireChildren( &data_field, index, data );
    }
    else
    {
        AddUnnamedWireChildren( &data_field, data );
    }

    packet->Add( std::move( data_field ) );
}

// A virtual wire packet: a count byte followed by 2 * (count + 1) bytes of
// index/data pairs. Section 4.2.2, p.57 -- the count is 0 based and its top
// two bits are reserved.
bool ReadVwirePacket( PhaseReader* reader, Field* parent )
{
    uint8_t count_byte = 0;
    ByteSpan count_span{};
    if( !reader->Read( &count_byte, &count_span ) )
        return false;

    const unsigned groups = VwireGroupCount( count_byte );

    Field packet( "Virtual Wire Packet", Plural( groups, "group" ), count_byte, 8, count_span );
    packet.Add( Field( "Count", Hex( count_byte, 2 ) + "  " + Plural( groups, "group" ), count_byte, 8, count_span ) );

    const uint8_t count_reserved = VwireCountReservedBits( count_byte );
    if( count_reserved != 0 )
    {
        Field f( "Reserved", Hex( count_reserved, 2 ) + "  count bits [7:6] must be driven to 0", count_reserved, 8, count_span );
        f.severity = Severity::Warning;
        packet.Add( std::move( f ) );
    }

    for( unsigned g = 0; g < groups; ++g )
    {
        uint8_t index = 0;
        uint8_t data = 0;
        ByteSpan index_span{};
        ByteSpan data_span{};
        if( !reader->Read( &index, &index_span ) || !reader->Read( &data, &data_span ) )
        {
            parent->Add( std::move( packet ) );
            return false;
        }
        AddVwireGroup( &packet, index, index_span, data, data_span );
    }

    parent->Add( std::move( packet ) );
    return true;
}

// --- peripheral channel cycle headers -------------------------------------

// Payload bytes rendered as hex, truncated so a 4 KB write does not produce a
// 12,000 character line. The count is always stated in full alongside.
std::string HexBytes( const std::vector<uint8_t>& bytes, size_t limit )
{
    std::string out;
    for( size_t i = 0; i < bytes.size() && i < limit; ++i )
    {
        if( i != 0 )
            out += ' ';
        char buf[ 8 ];
        std::snprintf( buf, sizeof( buf ), "%02X", bytes[ i ] );
        out += buf;
    }
    if( bytes.size() > limit )
        out += " ...";
    return out;
}

// The Length field, read under whichever of section 4.1.3's three rules the
// cycle type falls under. This is the single most consequential thing on this
// path that Table 5 and the figures do not state: a Length of 000h on a memory
// write is 4096 bytes, and reading it as zero loses the entire payload while
// looking completely reasonable.
Field LengthField( uint16_t raw, CycleLength meaning, unsigned* payload_bytes, ByteSpan span )
{
    Field f( "Length", Hex( raw, 3 ), raw, static_cast<uint8_t>( CycleLengthBits() ), span );

    switch( meaning )
    {
    case CycleLength::OneBased:
    {
        const unsigned resolved = CycleResolvedLength( raw );
        f.text += "  " + Plural( resolved, "byte" );
        if( raw == 0 )
            f.text += ", all zeros means 4 KB";
        *payload_bytes = resolved;
        break;
    }
    case CycleLength::MustBeZero:
        // "the length field must be driven to zeros by initiator. The receiver
        // must ignore the length field." So this is not a count at all, and a
        // nonzero value is the initiator misbehaving rather than a payload.
        f.text += raw == 0 ? "  driven to zero, as required" : "  must be driven to zero by the initiator";
        if( raw != 0 )
            f.severity = Severity::Warning;
        *payload_bytes = 0;
        break;
    case CycleLength::Reserved:
        f.text += raw == 0 ? "  Reserved for this cycle type, sent as all 0s" : "  Reserved for this cycle type, must be all 0s";
        if( raw != 0 )
            f.severity = Severity::Warning;
        *payload_bytes = 0;
        break;
    case CycleLength::BlockErase:
    {
        // Table 16, p.81. Not a byte count: an erase block size encoding whose
        // meaning depends on the flash sharing scheme, and the scheme is set in
        // configuration register 040h bit 11 rather than carried in the packet.
        //
        // Reporting one size would be right half the time and silent about it,
        // so both readings are named. 1h is the sharp case: 32 KB under one
        // scheme and 4 KB under the other.
        const FlashEraseSize size = LookupFlashEraseSize( raw );
        f.text += "  erase block size";
        if( size.target_attached == nullptr && size.controller_attached == nullptr )
        {
            f.text += ", Reserved under both flash sharing schemes";
            f.severity = Severity::Warning;
        }
        else
        {
            f.text += std::string( ", target attached " )
                      + ( size.target_attached != nullptr ? size.target_attached : "Reserved" ) + ", controller attached "
                      + ( size.controller_attached != nullptr ? size.controller_attached : "Reserved" );
        }
        *payload_bytes = 0;
        break;
    }
    }
    return f;
}

// The four message specific bytes of Figure 38, resolved when the message code
// is one Table 6 names. LTR is the only one the base specification defines.
void AddMessageSpecific( Field* parent, uint8_t code, const uint8_t bytes[ 4 ], ByteSpan span )
{
    MessageCodeInfo msg;
    if( !LookupMessageCode( code, &msg ) || !msg.fields_transcribed )
    {
        // Figure 38 gives every message four of these and says nothing about
        // what is in them; only the message code decides, and this one is not
        // a code with a transcribed layout.
        for( int i = 0; i < 4; ++i )
            parent->Add( Field( "Message Specific Byte " + std::to_string( i ), Hex( bytes[ i ], 2 ), bytes[ i ], 8, span ) );
        return;
    }

    const LtrMessage ltr = DecodeLtrMessage( bytes[ 0 ], bytes[ 1 ] );

    parent->Add( Field( "Requirement", BitText( 7, ltr.requirement ? 1u : 0u )
                                           + ( ltr.requirement ? "  latency fields below are valid"
                                                               : "  target has no service requirement" ),
                        ltr.requirement ? 1u : 0u, 1, span ) );

    if( ltr.requirement )
    {
        const char* scale_text = LtrScaleText( ltr.scale );
        Field scale( "Latency Scale", Hex( ltr.scale, 1 ) + "  " + ( scale_text != nullptr ? scale_text : "Reserved" ),
                     ltr.scale, 3, span );
        if( scale_text == nullptr )
            scale.severity = Severity::Warning;
        parent->Add( std::move( scale ) );

        Field value( "Latency Value", Hex( ltr.value, 3 ), ltr.value, 10, span );
        uint32_t multiplier = 0;
        if( LtrScaleNanoseconds( ltr.scale, &multiplier ) )
        {
            const uint64_t ns = static_cast<uint64_t>( ltr.value ) * multiplier;
            value.text += "  " + std::to_string( ns ) + " ns";
            // "Setting the Latency Value field to all 0's indicates that the
            // eSPI target will be impacted by any delay and that the best
            // possible service is requested" (p.56).
            if( ltr.value == 0 )
                value.text += ", best possible service requested";
        }
        parent->Add( std::move( value ) );
    }
    else
    {
        // Table 7: the remaining fields are only valid when RQ is set. Naming
        // them without their values is the honest rendering, the same way a
        // virtual wire under a clear valid bit is named but not read.
        parent->Add( Note( "Masked", "Latency Scale, Latency Value  requirement bit clear, fields not valid", span ) );
    }

    if( ltr.reserved != 0 )
    {
        Field r( "Reserved", Hex( ltr.reserved, 1 ) + "  bits [6:5] must be driven to 0", ltr.reserved, 2, span );
        r.severity = Severity::Warning;
        parent->Add( std::move( r ) );
    }

    // Figure 40 draws bytes 6 and 7 as Reserved outright.
    for( int i = 2; i < 4; ++i )
    {
        if( bytes[ i ] == 0 )
            continue;
        Field r( "Reserved", "byte " + std::to_string( i + 4 ) + " = " + Hex( bytes[ i ], 2 ) + "  must be driven to 0",
                 bytes[ i ], 8, span );
        r.severity = Severity::Warning;
        parent->Add( std::move( r ) );
    }
}

// Name the variable field a cycle type encoding carries, if it carries one.
// Which field that is comes from the table row, not from the bit pattern --
// P1P0, r2r1r0 and R1R0 sit in three different and partly overlapping ranges.
void AddCycleVariable( Field* parent, const CycleTypeInfo& info, uint8_t cycle_type, ByteSpan span )
{
    const uint8_t value = CycleVariableValue( info, cycle_type );

    switch( info.variable )
    {
    case CycleVariable::SplitCompletion:
    {
        const char* text = SplitCompletionText( value );
        Field f( "Split Completion", Hex( value, 1 ) + "  " + ( text != nullptr ? text : "undefined" ), value, 2, span );
        if( SplitCompletionViolatesNote2( info, cycle_type ) )
        {
            f.text += " -- but P1 must be 1 on an Unsuccessful Completion without Data, which is always the last or "
                      "the only completion";
            f.severity = Severity::Error;
        }
        parent->Add( std::move( f ) );
        break;
    }
    case CycleVariable::MessageRouting:
    {
        const char* text = MessageRoutingText( value );
        Field f( "Routing", Hex( value, 1 ) + "  " + ( text != nullptr ? text : "Reserved" ), value, 3, span );
        if( text == nullptr )
            f.severity = Severity::Warning;
        parent->Add( std::move( f ) );
        break;
    }
    case CycleVariable::RpmcTarget:
    {
        const char* text = RpmcTargetText( value );
        parent->Add( Field( "RPMC Target", Hex( value, 1 ) + "  " + ( text != nullptr ? text : "undefined" ), value, 2, span ) );
        break;
    }
    case CycleVariable::None:
        break;
    }
}

// One cycle-type-headed packet header. Returns false when the header cannot be
// read to its end -- either the source ran out or the cycle type gives no
// length to read to, in which case the caller stops rather than guessing.
bool ReadCycleHeader( PhaseReader* reader, Field* parent, PacketContext* ctx )
{
    ctx->have_cycle_header = false;
    ctx->payload_bytes = 0;
    ctx->payload_kind = CyclePayload::Opaque;

    uint8_t cycle_type = 0;
    ByteSpan type_span{};
    if( !reader->Read( &cycle_type, &type_span ) )
        return false;

    CycleTypeInfo info;
    if( !LookupCycleType( ctx->channel, cycle_type, &info ) )
    {
        // Deliberately not "unknown cycle type": the same byte may well be a
        // defined cycle type on another channel, and saying which channel was
        // consulted is what turns this from a dead end into a diagnosis.
        parent->Add( ErrorField( "Cycle Type", Hex( cycle_type, 2 ) + "  no cycle type with this encoding on the "
                                                   + ChannelName( ctx->channel )
                                                   + " channel -- packet length unknown, decode stopped" ) );
        ctx->stopped = true;
        return false;
    }

    // Only the command phase's cycle type says what kind of transaction this
    // is. The response phase's, when there is one, is the completion coming
    // back, and a completion is never the thing DEFER would be answering.
    if( reader->PhaseOf() == Phase::Command )
        ctx->command_is_posted = ( info.command_type == CycleCommandType::Posted );

    Field type_field( "Cycle Type", Hex( cycle_type, 2 ) + "  " + info.name, cycle_type, 8, type_span );
    type_field.Add( Field( "Command Type", CycleCommandTypeName( info.command_type ), 0, 8, type_span ) );
    type_field.Add( Field( "Direction", CycleDirectionText( info.direction ), 0, 8, type_span ) );
    AddCycleVariable( &type_field, info, cycle_type, type_span );

    CycleHeaderLayout layout;
    if( !LookupCycleHeaderLayout( info.layout, &layout ) )
    {
        Field gap( "Packet Format",
                   std::string( info.name ) + " -- header layout not transcribed yet, packet length unknown, decode stopped",
                   cycle_type, 8, type_span );
        gap.severity = Severity::Warning;
        type_field.Add( std::move( gap ) );
        parent->Add( std::move( type_field ) );
        ctx->stopped = true;
        return false;
    }
    parent->Add( std::move( type_field ) );

    // Byte 1 is Tag over Length[11:8] and byte 2 is Length[7:0] -- Figure 33,
    // p.46, and every packet figure repeats it.
    uint8_t byte1 = 0;
    uint8_t byte2 = 0;
    ByteSpan tag_span{};
    ByteSpan len_span{};
    if( !reader->Read( &byte1, &tag_span ) || !reader->Read( &byte2, &len_span ) )
        return false;

    const uint8_t tag = CycleTagOf( byte1 );
    parent->Add( Field( "Tag", Hex( tag, 1 ), tag, 4, tag_span ) );

    unsigned payload = 0;
    parent->Add( LengthField( CycleLengthOf( byte1, byte2 ), layout.length, &payload, Merge( tag_span, len_span ) ) );
    ctx->payload_bytes = layout.has_payload ? payload : 0;
    ctx->payload_kind = layout.payload;

    if( layout.address_bytes != 0 )
    {
        uint64_t address = 0;
        ByteSpan addr_span{};
        if( !reader->ReadInteger( layout.address_bytes, /*msb_first=*/true, &address, &addr_span ) )
            return false;
        Field addr( "Address", Hex( address, layout.address_bytes * 2 ), address,
                    static_cast<uint8_t>( layout.address_bytes * 8 ), addr_span );
        // Section 4.1.4, p.51: "When 64 bits addressing format is used, the
        // upper 32 bits address [63:32] must not be all 0" -- an address below
        // 4 GB has to use the 32-bit format.
        if( layout.address_bytes == 8 && ( address >> 32 ) == 0 )
        {
            addr.text += "  below 4 GB, which must use the 32 bit addressing format";
            addr.severity = Severity::Warning;
        }
        parent->Add( std::move( addr ) );
    }

    if( layout.has_message_code )
    {
        uint8_t code = 0;
        ByteSpan code_span{};
        if( !reader->Read( &code, &code_span ) )
            return false;

        MessageCodeInfo msg;
        const bool named = LookupMessageCode( code, &msg );
        Field code_field( "Message Code", Hex( code, 2 ) + ( named ? std::string( "  " ) + msg.name + ", " + msg.description
                                                                  : std::string( "  not a message code Table 6 names" ) ),
                          code, 8, code_span );
        if( !named )
            code_field.severity = Severity::Warning;

        uint8_t specific[ 4 ] = { 0, 0, 0, 0 };
        ByteSpan specific_span{};
        for( int i = 0; i < 4; ++i )
        {
            ByteSpan one{};
            if( !reader->Read( &specific[ i ], &one ) )
            {
                parent->Add( std::move( code_field ) );
                return false;
            }
            specific_span = ( i == 0 ) ? one : Merge( specific_span, one );
        }
        AddMessageSpecific( &code_field, code, specific, specific_span );
        parent->Add( std::move( code_field ) );
    }

    ctx->have_cycle_header = true;
    return true;
}

// Read `count` bytes into a buffer, merging their spans.
bool ReadBytes( PhaseReader* reader, unsigned count, std::vector<uint8_t>* bytes, ByteSpan* span )
{
    bytes->reserve( bytes->size() + count );
    for( unsigned i = 0; i < count; ++i )
    {
        uint8_t byte = 0;
        ByteSpan one{};
        if( !reader->Read( &byte, &one ) )
            return false;
        bytes->push_back( byte );
        *span = ( i == 0 ) ? one : Merge( *span, one );
    }
    return true;
}

// The five MCTP header bytes of Figure 46, p.74. Named but not interpreted:
// this document labels the cells and never says what any value means, so a
// decode that explained a Message Tag would be explaining DSP0237, which is
// not in front of us.
void AddMctpHeader( Field* parent, const uint8_t bytes[ 5 ], ByteSpan span )
{
    MctpHeaderField fields[ 16 ];
    const size_t count = DecodeMctpHeader( bytes, fields, 16 );

    for( size_t i = 0; i < count && i < 16; ++i )
    {
        const MctpHeaderField& f = fields[ i ];
        const bool single = ( f.high == f.low );
        std::string text = single ? BitText( f.low, f.value ) : Hex( f.value, 2 );
        parent->Add( Field( f.name, text, f.value, static_cast<uint8_t>( f.high - f.low + 1 ), span ) );
    }

    // Figure 46 draws bit 0 of the Source Target Address byte as a literal 1,
    // in its own cell against the bit ruler, the same way Figure 45 draws the
    // destination byte's bit 0 as a literal 0.
    const uint8_t bit0 = MctpSourceBit0( bytes[ 0 ] );
    if( bit0 != MctpSourceBit0Expected() )
    {
        Field f( "Source Address bit 0", BitText( 0, bit0 ) + "  Figure 46 draws this bit as "
                                             + std::to_string( MctpSourceBit0Expected() ),
                 bit0, 1, span );
        f.severity = Severity::Warning;
        parent->Add( std::move( f ) );
    }
}

// The OOB packet's data region, read as the tunneled SMBus block write of
// Figure 45, p.73.
//
// The whole SMBus packet is data as far as eSPI is concerned -- the eSPI header
// is three bytes and stops. What makes this worth decoding rather than dumping
// is that the packet states its own length twice, in the OOB Length field and
// in the SMBus Byte Count, and the difference between them is the only thing
// that says whether the last byte is a PEC.
bool ReadSmbusPayload( PhaseReader* reader, Field* parent, const PacketContext& ctx )
{
    std::vector<uint8_t> header;
    ByteSpan header_span{};
    if( !ReadBytes( reader, SmbusHeaderBytes(), &header, &header_span ) )
        return false;

    const SmbusPacketInfo smbus = DecodeSmbusPacket( ctx.payload_bytes, header[ 0 ], header[ 1 ], header[ 2 ] );

    Field packet( "SMBus Packet", Plural( ctx.payload_bytes, "byte" ), 0, 8, header_span );

    Field address( "Target Address", Hex( header[ 0 ], 2 ) + "  address " + Hex( smbus.address, 2 ), smbus.address,
                   static_cast<uint8_t>( SmbusAddressBits() ), header_span );
    if( smbus.address_bit0 != SmbusAddressBit0Expected() )
    {
        address.text += ", but bit 0 is " + std::to_string( smbus.address_bit0 ) + " and Figure 45 draws it as "
                        + std::to_string( SmbusAddressBit0Expected() );
        address.severity = Severity::Warning;
    }
    packet.Add( std::move( address ) );

    SmbusCommandInfo command;
    const bool named_command = LookupSmbusCommand( smbus.command, &command );
    packet.Add( Field( "Command Opcode",
                       Hex( smbus.command, 2 )
                           + ( named_command ? std::string( "  " ) + command.name
                                             : std::string( "  not a command opcode this specification names" ) ),
                       smbus.command, 8, header_span ) );

    packet.Add( Field( "Byte Count", Hex( smbus.byte_count, 2 ) + "  " + Plural( smbus.byte_count, "byte" )
                                         + ", excluding the 3 SMBus header bytes and the PEC",
                       smbus.byte_count, 8, header_span ) );

    if( !smbus.consistent )
    {
        // Length says one thing and Byte Count says another. The packet still
        // decodes -- the OOB Length alone decides how many bytes to read -- so
        // nothing else on the bus would notice this.
        packet.Add( ErrorField( "SMBus Byte Count",
                                "Length " + std::to_string( ctx.payload_bytes ) + " leaves "
                                    + std::to_string( smbus.pec_bytes ) + " bytes after the 3 header bytes and "
                                    + std::to_string( smbus.byte_count )
                                    + " counted bytes, and the only byte allowed there is an optional PEC" ) );
        // Fall back to reading the rest as opaque bytes: the count cannot be
        // trusted to say where the data ends.
        std::vector<uint8_t> rest;
        ByteSpan rest_span{};
        const unsigned remaining = ctx.payload_bytes - SmbusHeaderBytes();
        if( remaining != 0 )
        {
            if( !ReadBytes( reader, remaining, &rest, &rest_span ) )
            {
                parent->Add( std::move( packet ) );
                return false;
            }
            packet.Add( Field( "Data", Plural( remaining, "byte" ) + "  " + HexBytes( rest, 16 ), 0, 8, rest_span ) );
        }
        parent->Add( std::move( packet ) );
        return true;
    }

    unsigned data_bytes = smbus.byte_count;

    if( named_command && command.header_bytes != 0 && data_bytes >= command.header_bytes )
    {
        std::vector<uint8_t> mctp;
        ByteSpan mctp_span{};
        if( !ReadBytes( reader, command.header_bytes, &mctp, &mctp_span ) )
        {
            parent->Add( std::move( packet ) );
            return false;
        }
        Field header_field( std::string( command.name ) + " Header", Plural( command.header_bytes, "byte" ), 0, 8, mctp_span );
        AddMctpHeader( &header_field, mctp.data(), mctp_span );
        packet.Add( std::move( header_field ) );
        data_bytes -= command.header_bytes;
    }
    else if( named_command && command.header_bytes != 0 )
    {
        // The Byte Count comprehends the embedded protocol's header, so a count
        // shorter than that header cannot be an MCTP packet however it is
        // labelled. Figure 46's worked example is Byte Count = 5 + 64.
        Field f( std::string( command.name ) + " Header",
                 "Byte Count is " + std::to_string( smbus.byte_count ) + " and the header alone is "
                     + std::to_string( command.header_bytes ) + " bytes",
                 0, 8, header_span );
        f.severity = Severity::Warning;
        packet.Add( std::move( f ) );
    }

    if( data_bytes != 0 )
    {
        std::vector<uint8_t> data;
        ByteSpan data_span{};
        if( !ReadBytes( reader, data_bytes, &data, &data_span ) )
        {
            parent->Add( std::move( packet ) );
            return false;
        }
        packet.Add( Field( "Data", Plural( data_bytes, "byte" ) + "  " + HexBytes( data, 16 ), 0, 8, data_span ) );
    }

    if( smbus.pec_bytes == 1 )
    {
        uint8_t pec = 0;
        ByteSpan pec_span{};
        if( !reader->Read( &pec, &pec_span ) )
        {
            parent->Add( std::move( packet ) );
            return false;
        }
        packet.Add( Field( "PEC", Hex( pec, 2 ), pec, 8, pec_span ) );
    }

    parent->Add( std::move( packet ) );
    return true;
}

// The data bytes a cycle header's Length counted. Emits nothing when there are
// none: "Payload 0 bytes" on every memory read request is noise, and the cycle
// type name already says whether the packet carries data.
bool ReadPayload( PhaseReader* reader, Field* parent, const PacketContext& ctx )
{
    if( !ctx.have_cycle_header || ctx.payload_bytes == 0 )
        return true;

    if( ctx.payload_kind == CyclePayload::SmbusPacket )
    {
        if( ctx.payload_bytes >= SmbusHeaderBytes() )
            return ReadSmbusPayload( reader, parent, ctx );

        // "The Length field of the OOB message comprehends the count by the
        // SMBus Byte Count field, in addition to the 3 header bytes" (p.73), so
        // there is no OOB message shorter than those three bytes. Read what is
        // there rather than walking off the end of the packet.
        Field f( "SMBus Packet",
                 "Length " + std::to_string( ctx.payload_bytes ) + " is shorter than the 3 SMBus header bytes", 0, 8,
                 ByteSpan{} );
        f.severity = Severity::Error;
        parent->Add( std::move( f ) );
    }

    std::vector<uint8_t> bytes;
    ByteSpan span{};
    if( !ReadBytes( reader, ctx.payload_bytes, &bytes, &span ) )
        return false;

    Field data( "Data", Plural( ctx.payload_bytes, "byte" ) + "  " + HexBytes( bytes, 16 ), 0, 8, span );

    // Figure 50, p.76, names data byte 0 of an RPMC OP1 request "RPMC Opcode:
    // OP1". The value it should hold is in configuration register 040h bits
    // 31:24 for the first flash device and in 048h/04Ch for the rest, so the
    // decoder names the byte and leaves the comparison to a reader who has the
    // configuration in front of them.
    if( ctx.payload_kind == CyclePayload::RpmcOpcode )
        data.Add( Field( "RPMC Opcode", Hex( bytes[ 0 ], 2 ) + "  OP1", bytes[ 0 ], 8, span ) );

    parent->Add( std::move( data ) );
    return true;
}

// The 1, 2 or 4 data bytes of a short cycle. The count is C1C0 in the opcode
// (Table 2 note 1, p.27); there is no length field on the wire at all.
bool ReadShortData( PhaseReader* reader, Field* parent, const PacketContext& ctx )
{
    std::vector<uint8_t> bytes;
    bytes.reserve( ctx.short_length );
    ByteSpan span{};
    for( unsigned i = 0; i < ctx.short_length; ++i )
    {
        uint8_t byte = 0;
        ByteSpan one{};
        if( !reader->Read( &byte, &one ) )
            return false;
        bytes.push_back( byte );
        span = ( i == 0 ) ? one : Merge( span, one );
    }

    parent->Add( Field( "Data", Plural( ctx.short_length, "byte" ) + "  " + HexBytes( bytes, 16 ), 0, 8, span ) );
    return true;
}

// --- the In-band RESET command, section 8.3.2 pp.122-123 -------------------

// Everything after the RESET opcode is ignored -- "Ignore all the subsequent
// bits received" (p.122) -- and the transaction ends at the chip select
// deassertion edge rather than at a length anything could compute, because
// p.123 has the target "Wait until CS# deassertion". So the bytes are read to
// the end of the frame and reported, never interpreted.
//
// Read through ReadUncovered because there is no CRC to accumulate them into:
// "No CRC byte and thus CRC checking must be ignored."
void ReadResetRemainder( PhaseReader* reader, Field* parent, IoMode mode, PacketContext* ctx )
{
    ctx->in_band_reset = true;

    std::vector<uint8_t> bytes;
    ByteSpan span{};
    for( ;; )
    {
        uint8_t value = 0;
        ByteSpan one{};
        if( !reader->ReadUncovered( &value, &one ) )
            break;
        span = bytes.empty() ? one : Merge( span, one );
        bytes.push_back( value );
    }

    const unsigned count = static_cast<unsigned>( bytes.size() );
    Field ignored( "Ignored", Plural( count, "byte" ) + ( count != 0 ? "  " + HexBytes( bytes, 16 ) : std::string() ), 0, 8,
                   span );

    // Figure 65, p.123, draws the whole command phase: sixteen clocks with
    // every I/O line high. The opcode is the first of those clocks, so it
    // counts. Neither the clock total nor the all-ones content is a length the
    // decoder reads to -- both are checked against what the frame actually
    // held. A deviation is the controller's, and it is a warning rather than an
    // error because the target ignores these bits either way and resets
    // regardless; nothing else on the bus would flag it.
    const unsigned clocks = ( count + 1 ) * static_cast<unsigned>( ClocksPerByte( mode ) );
    bool all_high = true;
    for( uint8_t value : bytes )
    {
        if( value != 0xFF )
            all_high = false;
    }

    if( clocks != ResetCommandClocks() || !all_high )
    {
        std::string differs;
        if( clocks != ResetCommandClocks() )
            differs = "is " + std::to_string( clocks ) + " clocks";
        if( !all_high )
        {
            if( !differs.empty() )
                differs += " and ";
            differs += "has ignored bytes that are not all FFh";
        }

        Field f( "Reset Pattern",
                 "Figure 65 draws " + std::to_string( ResetCommandClocks() ) + " clocks with every I/O line high; this frame "
                     + differs,
                 0, 8, ByteSpan{} );
        f.severity = Severity::Warning;
        ignored.Add( std::move( f ) );
    }

    parent->Add( std::move( ignored ) );

    // p.123: "Offset 008h-00Bh: General Capabilities and Configurations" is the
    // only register an In-band RESET returns to its default, and it happens at
    // the chip select deassertion edge rather than when the opcode is seen.
    // Worth saying out loud because 008h is where I/O Mode Select and CRC
    // Checking Enable live, so this is the bus going back to Single I/O with
    // CRC checking off. Transaction::session carries that to espi::SessionState,
    // which is what follows it across transactions.
    //
    // The register's name comes from Table 21 rather than being repeated here,
    // so the tree states it once.
    const char* name = nullptr;
    LookupConfigRegister( ResetRegisterStart(), &name );

    char offsets[ 32 ];
    std::snprintf( offsets, sizeof( offsets ), "%03Xh-%03Xh", ResetRegisterStart(), ResetRegisterEnd() );

    parent->Add( Field( "Register Reset",
                        std::string( offsets ) + ( name != nullptr ? std::string( " " ) + name : std::string() )
                            + " at the chip select deassertion edge; every other register retains its value",
                        ResetRegisterStart(), 16, ByteSpan{} ) );
}

// Walk one phase's elements. Returns false if the source ran out mid-packet.
bool ReadElements( PhaseReader* reader, const ElementList& list, Field* parent, PacketContext* config )
{
    for( uint8_t i = 0; i < list.count; ++i )
    {
        const Element element = list.items[ i ];

        if( element == Element::VwirePacket )
        {
            if( !ReadVwirePacket( reader, parent ) )
                return false;
            continue;
        }
        if( element == Element::CycleHeader )
        {
            if( !ReadCycleHeader( reader, parent, config ) )
                return false;
            continue;
        }
        if( element == Element::Payload )
        {
            if( !ReadPayload( reader, parent, *config ) )
                return false;
            continue;
        }
        if( element == Element::ShortData )
        {
            if( !ReadShortData( reader, parent, *config ) )
                return false;
            continue;
        }

        const size_t size = ElementFixedSize( element );
        // Addresses are big endian everywhere they appear: the configuration
        // register address (section 5.1, p.86) and both short cycle address
        // widths (Figures 35 and 37, whose Byte 0 is the most significant).
        const bool msb_first =
            ( element == Element::Addr16 || element == Element::IoAddr16 || element == Element::MemAddr32 );
        uint64_t value = 0;
        ByteSpan span{};
        if( !reader->ReadInteger( size, msb_first, &value, &span ) )
            return false;

        const int digits = static_cast<int>( size ) * 2;
        std::string text = Hex( value, digits );

        // Addr16 only ever appears in GET_CONFIGURATION and SET_CONFIGURATION,
        // so naming the register here also tells the Data32 that follows --
        // possibly in the other phase -- what it is looking at.
        Severity address_severity = Severity::Info;
        if( element == Element::Addr16 )
        {
            config->have_address = true;
            config->address = static_cast<uint16_t>( value );
            text += DescribeConfigAddress( config->address, &address_severity );
        }

        Field field( ElementName( element ), text, value, static_cast<uint8_t>( size * 8 ), span );
        field.severity = address_severity;

        if( element == Element::Status16 )
            AddStatusChildren( &field, static_cast<uint16_t>( value ) );
        else if( element == Element::Data32 && config->have_address )
            AddConfigChildren( &field, config->address, static_cast<uint32_t>( value ) );

        // A DWord written to 008h in the command phase is the one packet that
        // changes how the bus itself is read from the next chip select onward.
        // Recorded here, applied nowhere: whether it takes effect depends on a
        // response byte this phase has not reached yet.
        if( element == Element::Data32 && reader->PhaseOf() == Phase::Command && config->have_address
            && IsGeneralConfigAddress( config->address ) )
        {
            config->general_config_write =
                DecodeGeneralConfig( config->address, static_cast<uint32_t>( value ), &config->general_config );
        }

        parent->Add( std::move( field ) );
    }
    return true;
}

// Walk one chip-select-delimited transaction into `out`.
//
// LIFTED OUT OF LinkDecoder::Decode SO THAT IT HAS ONE EXIT. A malformed
// packet is a decode that stops early rather than a failure, so this returns
// from fourteen places, and the session update below has to be worked out
// after every one of them -- including the ones that never reach a response
// byte, which is exactly the case §8.3.2 p.122 calls uncertain. Fourteen
// copies of that reasoning is fourteen chances to leave one out.
void DecodeTransaction( ByteSource* source, Transaction* out, PacketContext* context )
{
    Transaction& txn = *out;
    PacketContext& config = *context;

    // ----------------------------------------------------------------- command
    PhaseReader cmd( source, Phase::Command );
    Field command( "Command", "", 0, 8, ByteSpan{} );

    uint8_t opcode = 0;
    ByteSpan opcode_span{};
    if( !cmd.Read( &opcode, &opcode_span ) )
    {
        txn.truncated = true;
        txn.fields.push_back( std::move( command ) );
        return;
    }

    OpcodeInfo info;
    if( !LookupOpcode( opcode, &info ) )
    {
        command.Add( ErrorField( "Opcode", Hex( opcode, 2 ) + "  not a defined command opcode" ) );
        txn.fields.push_back( std::move( command ) );
        return;
    }

    command.Add( MakeField( "Opcode", Hex( opcode, 2 ) + "  " + info.name, opcode, 8, opcode_span ) );

    // The channel decides which cycle type table applies -- Table 5 note 3,
    // p.49, is explicit that the encodings repeat across channels.
    config.channel = info.channel;

    if( info.has_short_length )
    {
        // C1C0 is not a label: it is the only statement of how many data bytes
        // the packet holds, so a reserved encoding leaves the packet length
        // unknown and the decode has to stop rather than pick a size.
        if( info.length_reserved )
        {
            command.Add( ErrorField( "Request Length",
                                     "C1C0 = 10b is Reserved -- packet length unknown, decode stopped" ) );
            txn.fields.push_back( std::move( command ) );
            return;
        }
        config.short_length = info.request_length;
        command.Add( MakeField( "Request Length", Plural( info.request_length, "byte" ), info.request_length, 8, opcode_span ) );
    }

    PacketShape shape;
    if( !LookupShape( opcode, &shape ) )
    {
        command.Add( ErrorField( "Packet Shape",
                                 std::string( "no shape transcribed for " ) + info.name
                                     + " -- packet length unknown, decode stopped" ) );
        txn.fields.push_back( std::move( command ) );
        return;
    }

    // Section 8.3.2, p.122: RESET carries no CRC byte and gets no response
    // phase, so the transaction ends where the chip select does and there is
    // nothing after this point to read. Every row the shape table marks
    // NoCrcNoResponse takes this path; the header of
    // src/core/tables/PacketShapes.h is where the reasoning lives.
    //
    // Running out of bytes is the normal end here, so `truncated` stays false.
    if( shape.framing == PacketFraming::NoCrcNoResponse )
    {
        ReadResetRemainder( &cmd, &command, source->Mode(), &config );
        command.span = cmd.Span();
        txn.fields.push_back( std::move( command ) );
        txn.span = txn.fields.front().span;
        return;
    }

    if( !ReadElements( &cmd, shape.command, &command, &config ) )
    {
        txn.truncated = !config.stopped;
        txn.fields.push_back( std::move( command ) );
        return;
    }

    const uint8_t command_crc = cmd.Crc();
    uint8_t received_crc = 0;
    ByteSpan crc_span{};
    if( !cmd.ReadUncovered( &received_crc, &crc_span ) )
    {
        txn.truncated = true;
        txn.fields.push_back( std::move( command ) );
        return;
    }
    command.Add( CrcField( received_crc, command_crc, crc_span ) );
    command.span = cmd.Span();
    txn.fields.push_back( std::move( command ) );

    // --------------------------------------------------------------------- TAR
    ByteSpan tar_span{};
    if( !source->TurnAround( &tar_span ) )
    {
        txn.truncated = true;
        return;
    }
    txn.fields.push_back( MakeField( "TAR", Plural( kTurnAroundClocks, "clock" ), 0, 8, tar_span ) );

    // ---------------------------------------------------------------- response
    PhaseReader rsp( source, Phase::Response );
    Field response( "Response", "", 0, 8, ByteSpan{} );

    uint8_t response_byte = 0;
    ByteSpan response_span{};
    unsigned wait_states = 0;
    for( ;; )
    {
        // Wait states are read outside the CRC, so ReadUncovered, then fold in
        // the first byte that is not one.
        if( !rsp.ReadUncovered( &response_byte, &response_span ) )
        {
            txn.truncated = true;
            if( wait_states != 0 )
                response.Add( MakeField( "WAIT_STATE", Plural( wait_states, "byte-time" ) + " of delay", 0x0F, 8, ByteSpan{} ) );
            txn.fields.push_back( std::move( response ) );
            return;
        }
        if( !IsWaitState( response_byte ) )
            break;
        ++wait_states;
    }

    if( wait_states != 0 )
        response.Add( MakeField( "WAIT_STATE", Plural( wait_states, "byte-time" ) + " of delay, excluded from CRC", 0x0F, 8,
                                 ByteSpan{} ) );

    ResponseInfo rinfo;
    if( !LookupResponse( response_byte, &rinfo ) )
    {
        response.Add( ErrorField( "Response", Hex( response_byte, 2 ) + "  not a defined response encoding" ) );
        txn.fields.push_back( std::move( response ) );
        return;
    }

    // The response byte is covered by the response CRC (section 5.2, p.90);
    // only the WAIT_STATE bytes skipped above are not.
    rsp.CrcUpdate( response_byte );

    // ACCEPT is what "completes successfully" means for a configuration write
    // -- §5.2, p.90, and §8.3.2, p.122. Recorded here rather than at the end
    // because several of the exits below are past this point.
    config.accepted = ( rinfo.code == ResponseCode::Accept );

    Field response_field( "Response", Hex( response_byte, 2 ) + "  " + rinfo.name, response_byte, 8, response_span );
    if( rinfo.code == ResponseCode::FatalError )
        response_field.severity = Severity::Error;
    else if( rinfo.code == ResponseCode::NonFatalError )
        response_field.severity = Severity::Warning;

    // Section 3.9, p.42: "The valid responses for posted transactions
    // initiated by eSPI controller are ACCEPT, FATAL ERROR and NON-FATAL
    // ERROR. DEFER response for posted transaction is invalid."
    //
    // A deferred posted write decodes perfectly -- right length, right CRC,
    // every field in place -- and is a target breaking the protocol. Nothing
    // else on the bus flags it.
    if( rinfo.code == ResponseCode::Defer && config.command_is_posted )
    {
        response_field.text += "  -- DEFER is not a valid response to a posted transaction";
        response_field.severity = Severity::Error;
    }

    // NO_RESPONSE means the target never drove the phase at all -- there is no
    // payload, no status and no CRC to read.
    if( rinfo.code == ResponseCode::NoResponse )
    {
        response_field.severity = Severity::Warning;
        response.Add( std::move( response_field ) );
        txn.fields.push_back( std::move( response ) );
        return;
    }

    const bool modifier_applies = ( opcode == 0x25 ) && ( rinfo.code == ResponseCode::Accept );
    if( modifier_applies && rinfo.modifier != 0 )
        response_field.Add( Field( "Modifier", ResponseModifierName( rinfo.modifier ), rinfo.modifier, 2, response_span ) );
    if( rinfo.reserved != 0 )
    {
        Field f( "Reserved", "must be driven to 0", rinfo.reserved, 2, response_span );
        f.severity = Severity::Warning;
        response_field.Add( std::move( f ) );
    }
    response.Add( std::move( response_field ) );

    // The response byte is already folded in above, so the reader starts from
    // the payload and its CRC is combined with the response byte's.
    //
    // A response that is not ACCEPT carries no completion. Figures 24 and 25,
    // pp.38-39, are the same PUT_NP answered both ways -- `ACCEPT HDR DATA STS
    // CRC` and `DEFER STS CRC` -- so this is not cosmetic: it decides how many
    // bytes the phase holds and therefore which byte the CRC lands on.
    ElementList response_elements;
    for( uint8_t i = 0; i < shape.response.count; ++i )
    {
        const Element element = shape.response.items[ i ];
        if( rinfo.code != ResponseCode::Accept && ElementPresentOnlyOnAccept( element ) )
            continue;
        response_elements.items[ response_elements.count++ ] = element;
    }

    if( modifier_applies && rinfo.modifier != 0 )
    {
        // Table 3 note 1, p.30: the modifier says which packet is appended to
        // the response phase ahead of the status trailer. A virtual wire packet
        // is one element; the other two are a cycle-type header and its data.
        //
        // The channel has to be set from the modifier rather than from the
        // opcode. GET_STATUS is channel independent, but Table 5 note 3 means a
        // cycle type byte is meaningless without a channel -- an appended flash
        // completion read against the peripheral table would name the wrong
        // cycle type with complete confidence.
        ElementList appended;
        if( rinfo.modifier == 0x2 )
        {
            appended.items[ appended.count++ ] = Element::VwirePacket;
        }
        else
        {
            config.channel = ( rinfo.modifier == 0x1 ) ? ChannelId::Peripheral : ChannelId::Flash;
            appended.items[ appended.count++ ] = Element::CycleHeader;
            appended.items[ appended.count++ ] = Element::Payload;
        }
        for( uint8_t i = 0; i < response_elements.count && appended.count < ElementList::kMax; ++i )
            appended.items[ appended.count++ ] = response_elements.items[ i ];
        response_elements = appended;
    }

    if( !ReadElements( &rsp, response_elements, &response, &config ) )
    {
        txn.truncated = !config.stopped;
        txn.fields.push_back( std::move( response ) );
        return;
    }

    const uint8_t computed = rsp.Crc();
    uint8_t rsp_crc_byte = 0;
    ByteSpan rsp_crc_span{};
    if( !rsp.ReadUncovered( &rsp_crc_byte, &rsp_crc_span ) )
    {
        txn.truncated = true;
        txn.fields.push_back( std::move( response ) );
        return;
    }
    response.Add( CrcField( rsp_crc_byte, computed, rsp_crc_span ) );
    response.span = rsp.Span();
    txn.fields.push_back( std::move( response ) );

    txn.span = Merge( txn.fields.front().span, txn.fields.back().span );
}

// The settings that will be in force once this chip select deasserts, as their
// own top-level block.
//
// It carries no span on purpose. Nothing on the wire is being described -- the
// bytes that caused this are already decoded above, in the Data field or in
// Register Reset -- so there is no bubble to draw and the shell skips it. What
// it adds is the consequence, which is the one thing a reader cannot work out
// from the packet without the specification open beside them.
void AddSessionFields( Field* session, const GeneralConfig& config )
{
    Field mode( "I/O Mode", Hex( config.mode_encoding, 1 ), config.mode_encoding, 2, ByteSpan{} );
    if( config.mode_reserved )
    {
        // p.95 gives 11b no mode, so there is nothing to switch to. Saying so
        // and staying put is the only honest option: picking a mode here would
        // be an invented fact that then silently garbles every later byte.
        mode.text += "  Reserved, so the I/O mode is left as it was";
        mode.severity = Severity::Warning;
    }
    else
    {
        mode.text += std::string( "  " ) + IoModeName( config.mode ) + ", from this chip select's deassertion edge";
    }
    session->Add( std::move( mode ) );

    session->Add( Field( "CRC Checking",
                         std::string( config.crc_checking ? "1  enabled" : "0  disabled" )
                             + ", from this chip select's deassertion edge",
                         config.crc_checking ? 1u : 0u, 1, ByteSpan{} ) );
}

void RecordSessionUpdate( Transaction* txn, const PacketContext& config )
{
    if( config.in_band_reset )
    {
        // §8.3.2, p.123, names exactly one register, and the reset value comes
        // from the Default column of §6.2.1.3 rather than being restated here
        // -- Single I/O with CRC checking off, which is also where a capture
        // that starts at eSPI Reset# begins.
        GeneralConfig reset;
        if( !GeneralConfigResetState( &reset ) )
            return;

        txn->session.change = SessionChange::InbandReset;
        txn->session.config = reset;
    }
    else if( config.general_config_write && config.accepted )
    {
        txn->session.change = SessionChange::GeneralConfigWritten;
        txn->session.config = config.general_config;
    }
    else if( config.general_config_write )
    {
        // §8.3.2, p.122: "As the transaction does not complete successfully, it
        // is uncertain on the state of the interface settings after the error."
        // Nothing better than the previous mode is available to carry on with,
        // and saying so is the difference between a decode a reader can trust
        // and one they cannot.
        txn->session.change = SessionChange::GeneralConfigUncertain;

        Field session( "Session", "", 0, 8, ByteSpan{} );
        Field note( "Settings Uncertain",
                    "the write to General Capabilities and Configurations did not complete, so the I/O mode and CRC "
                    "checking in force after this chip select are unknown",
                    0, 8, ByteSpan{} );
        note.kind = FieldKind::Note;
        note.severity = Severity::Warning;
        session.Add( std::move( note ) );
        txn->fields.push_back( std::move( session ) );
        return;
    }

    if( txn->session.change == SessionChange::None )
        return;

    Field session( "Session", "", 0, 8, ByteSpan{} );
    AddSessionFields( &session, txn->session.config );
    txn->fields.push_back( std::move( session ) );
}

} // namespace

LinkDecoder::LinkDecoder( ByteSource* source ) : mSource( source )
{
}

bool LinkDecoder::Decode( Transaction* out )
{
    if( out == nullptr || !mSource->Active() )
        return false;

    *out = Transaction{};
    PacketContext config;
    DecodeTransaction( mSource, out, &config );
    RecordSessionUpdate( out, config );
    return true;
}

} // namespace espi
