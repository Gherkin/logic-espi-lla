#include "espi/LinkDecoder.h"

#include "espi/ConfigRegisters.h"
#include "espi/Crc8.h"
#include "espi/CycleTypes.h"
#include "espi/Opcodes.h"
#include "espi/PacketShape.h"
#include "espi/Responses.h"
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

    // The decode stopped on purpose, because something on the wire left the
    // packet length unknown -- not because the source ran out. The two look
    // identical to the caller of ReadElements and mean opposite things: one is
    // a chip select deasserting mid-packet, the other is a gap in what has
    // been transcribed, and reporting the second as the first would blame the
    // bus for the analyzer's own limits.
    bool stopped = false;
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

std::string BitText( unsigned bit, unsigned value )
{
    char buf[ 32 ];
    std::snprintf( buf, sizeof( buf ), "bit %u = %u", bit, value );
    return buf;
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
        data->Add( Field( "Masked", masked + "  valid bit clear, level not updated", 0, 8, data->span ) );

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
        data->Add( Field( "Masked", masked + "  valid bit clear, level not updated", 0, 8, data->span ) );
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
        parent->Add( Field( "Masked", "Latency Scale, Latency Value  requirement bit clear, fields not valid", 0, 8, span ) );
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

// The data bytes a cycle header's Length counted. Emits nothing when there are
// none: "Payload 0 bytes" on every memory read request is noise, and the cycle
// type name already says whether the packet carries data.
bool ReadPayload( PhaseReader* reader, Field* parent, const PacketContext& ctx )
{
    if( !ctx.have_cycle_header || ctx.payload_bytes == 0 )
        return true;

    std::vector<uint8_t> bytes;
    bytes.reserve( ctx.payload_bytes );
    ByteSpan span{};
    for( unsigned i = 0; i < ctx.payload_bytes; ++i )
    {
        uint8_t byte = 0;
        ByteSpan one{};
        if( !reader->Read( &byte, &one ) )
            return false;
        bytes.push_back( byte );
        span = ( i == 0 ) ? one : Merge( span, one );
    }

    parent->Add( Field( "Data", Plural( ctx.payload_bytes, "byte" ) + "  " + HexBytes( bytes, 16 ), 0, 8, span ) );
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

        parent->Add( std::move( field ) );
    }
    return true;
}

} // namespace

LinkDecoder::LinkDecoder( ByteSource* source ) : mSource( source )
{
}

bool LinkDecoder::Decode( Transaction* out )
{
    if( out == nullptr || !mSource->Active() )
        return false;

    Transaction txn;
    PacketContext config;

    // ----------------------------------------------------------------- command
    PhaseReader cmd( mSource, Phase::Command );
    Field command( "Command", "", 0, 8, ByteSpan{} );

    uint8_t opcode = 0;
    ByteSpan opcode_span{};
    if( !cmd.Read( &opcode, &opcode_span ) )
    {
        txn.truncated = true;
        txn.fields.push_back( std::move( command ) );
        *out = std::move( txn );
        return true;
    }

    OpcodeInfo info;
    if( !LookupOpcode( opcode, &info ) )
    {
        command.Add( ErrorField( "Opcode", Hex( opcode, 2 ) + "  not a defined command opcode" ) );
        txn.fields.push_back( std::move( command ) );
        *out = std::move( txn );
        return true;
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
            *out = std::move( txn );
            return true;
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
        *out = std::move( txn );
        return true;
    }

    if( !ReadElements( &cmd, shape.command, &command, &config ) )
    {
        txn.truncated = !config.stopped;
        txn.fields.push_back( std::move( command ) );
        *out = std::move( txn );
        return true;
    }

    const uint8_t command_crc = cmd.Crc();
    uint8_t received_crc = 0;
    ByteSpan crc_span{};
    if( !cmd.ReadUncovered( &received_crc, &crc_span ) )
    {
        txn.truncated = true;
        txn.fields.push_back( std::move( command ) );
        *out = std::move( txn );
        return true;
    }
    command.Add( CrcField( received_crc, command_crc, crc_span ) );
    command.span = cmd.Span();
    txn.fields.push_back( std::move( command ) );

    // --------------------------------------------------------------------- TAR
    ByteSpan tar_span{};
    if( !mSource->TurnAround( &tar_span ) )
    {
        txn.truncated = true;
        *out = std::move( txn );
        return true;
    }
    txn.fields.push_back( MakeField( "TAR", Plural( kTurnAroundClocks, "clock" ), 0, 8, tar_span ) );

    // ---------------------------------------------------------------- response
    PhaseReader rsp( mSource, Phase::Response );
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
            *out = std::move( txn );
            return true;
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
        *out = std::move( txn );
        return true;
    }

    // The response byte is covered by the response CRC (section 5.2, p.90);
    // only the WAIT_STATE bytes skipped above are not.
    rsp.CrcUpdate( response_byte );

    Field response_field( "Response", Hex( response_byte, 2 ) + "  " + rinfo.name, response_byte, 8, response_span );
    if( rinfo.code == ResponseCode::FatalError )
        response_field.severity = Severity::Error;
    else if( rinfo.code == ResponseCode::NonFatalError )
        response_field.severity = Severity::Warning;

    // NO_RESPONSE means the target never drove the phase at all -- there is no
    // payload, no status and no CRC to read.
    if( rinfo.code == ResponseCode::NoResponse )
    {
        response_field.severity = Severity::Warning;
        response.Add( std::move( response_field ) );
        txn.fields.push_back( std::move( response ) );
        *out = std::move( txn );
        return true;
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

    if( modifier_applies && rinfo.modifier == 0x2 )
    {
        // A virtual wire packet is appended ahead of the status trailer.
        ElementList appended;
        appended.items[ appended.count++ ] = Element::VwirePacket;
        for( uint8_t i = 0; i < response_elements.count && appended.count < ElementList::kMax; ++i )
            appended.items[ appended.count++ ] = response_elements.items[ i ];
        response_elements = appended;
    }
    else if( modifier_applies && rinfo.modifier != 0 )
    {
        response.Add( ErrorField( "Appended Packet",
                                  std::string( "response modifier " ) + Hex( rinfo.modifier, 1 )
                                      + " appends a completion, whose header layout is not transcribed yet" ) );
        txn.fields.push_back( std::move( response ) );
        *out = std::move( txn );
        return true;
    }

    if( !ReadElements( &rsp, response_elements, &response, &config ) )
    {
        txn.truncated = !config.stopped;
        txn.fields.push_back( std::move( response ) );
        *out = std::move( txn );
        return true;
    }

    const uint8_t computed = rsp.Crc();
    uint8_t rsp_crc_byte = 0;
    ByteSpan rsp_crc_span{};
    if( !rsp.ReadUncovered( &rsp_crc_byte, &rsp_crc_span ) )
    {
        txn.truncated = true;
        txn.fields.push_back( std::move( response ) );
        *out = std::move( txn );
        return true;
    }
    response.Add( CrcField( rsp_crc_byte, computed, rsp_crc_span ) );
    response.span = rsp.Span();
    txn.fields.push_back( std::move( response ) );

    txn.span = Merge( txn.fields.front().span, txn.fields.back().span );
    *out = std::move( txn );
    return true;
}

} // namespace espi
