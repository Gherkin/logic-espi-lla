#include "espi/LinkDecoder.h"

#include "espi/ConfigRegisters.h"
#include "espi/Crc8.h"
#include "espi/Opcodes.h"
#include "espi/PacketShape.h"
#include "espi/Responses.h"
#include "espi/Status.h"
#include "espi/VirtualWires.h"

#include <cstdio>
#include <string>
#include <utility>

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

// Which configuration register a packet is talking about. GET_CONFIGURATION
// puts the address in the command phase and the data in the response, so this
// has to survive the turn-around.
struct ConfigContext
{
    bool have_address = false;
    uint16_t address = 0;
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

// Walk one phase's elements. Returns false if the source ran out mid-packet.
bool ReadElements( PhaseReader* reader, const ElementList& list, Field* parent, ConfigContext* config )
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

        const size_t size = ElementFixedSize( element );
        const bool msb_first = ( element == Element::Addr16 );
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
    ConfigContext config;

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
    if( info.length_reserved )
        command.Add( ErrorField( "Request Length", "C1C0 = 10b is Reserved" ) );

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
        txn.truncated = true;
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
    ElementList response_elements = shape.response;
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
        txn.truncated = true;
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
