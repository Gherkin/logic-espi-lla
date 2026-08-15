#include "espi/LinkDecoder.h"

#include "espi/Crc8.h"
#include "espi/Opcodes.h"
#include "espi/PacketShape.h"
#include "espi/Responses.h"
#include "espi/Status.h"

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

// A virtual wire packet: a count byte followed by 2 * (count + 1) bytes of
// index/data pairs. Section 4.2.2, p.57 -- the count is 0 based and its top
// two bits are reserved.
bool ReadVwirePacket( PhaseReader* reader, Field* parent )
{
    uint8_t count_byte = 0;
    ByteSpan count_span{};
    if( !reader->Read( &count_byte, &count_span ) )
        return false;

    const unsigned groups = static_cast<unsigned>( count_byte & 0x3F ) + 1u;

    Field packet( "Virtual Wire Packet", Plural( groups, "group" ), count_byte, 8, count_span );
    packet.Add( Field( "Count", Hex( count_byte, 2 ) + "  " + Plural( groups, "group" ), count_byte, 8, count_span ) );

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
        packet.Add( Field( "Index", Hex( index, 2 ), index, 8, index_span ) );
        packet.Add( Field( "Data", Hex( data, 2 ), data, 8, data_span ) );
    }

    parent->Add( std::move( packet ) );
    return true;
}

// Walk one phase's elements. Returns false if the source ran out mid-packet.
bool ReadElements( PhaseReader* reader, const ElementList& list, Field* parent )
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
        Field field( ElementName( element ), Hex( value, digits ), value, static_cast<uint8_t>( size * 8 ), span );
        if( element == Element::Status16 )
            AddStatusChildren( &field, static_cast<uint16_t>( value ) );
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

    if( !ReadElements( &cmd, shape.command, &command ) )
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

    if( !ReadElements( &rsp, response_elements, &response ) )
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
