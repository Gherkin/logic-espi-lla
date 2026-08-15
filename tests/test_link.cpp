// T1 -- the link layer.
//
// Fixtures are literal bytes (R1) and expectations are literal text (R2). The
// .expected files were written out longhand from the fixture bytes and the
// specification; nothing here calls the decoder's own naming or formatting to
// build what it then compares against.
//
// Three fixtures come from tests/vectors/espi_dump.txt, a third-party decoder's
// export of real hardware. Two are hand built, and say so in their own headers
// -- the capture contains no wait states and no malformed traffic.
//
// The test cannot reach src/core/tables/ (R3), so the field names below were
// typed from the rendered spec pages, not lifted from the headers they check.

#include "espi/ConfigRegisters.h"
#include "espi/Decode.h"
#include "espi/LinkDecoder.h"
#include "espi/PacketShape.h"
#include "espi/Responses.h"
#include "espi/Status.h"
#include "support/FixtureByteSource.h"
#include "support/TestMacros.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace espi;
using espi_test::FixtureByteSource;
using espi_test::Frame;

namespace
{

std::string VectorPath( const char* name )
{
    return std::string( ESPI_VECTOR_DIR ) + "/link/" + name;
}

std::string ReadFile( const std::string& path, bool* ok )
{
    std::ifstream in( path );
    if( !in )
    {
        *ok = false;
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    *ok = true;
    return ss.str();
}

// Decode every transaction in a fixture and render it. Multiple transactions
// are separated by a --- line, which is this test's formatting, not the
// decoder's.
std::string DecodeFixture( const char* name, bool strict_consumption )
{
    std::vector<Frame> frames;
    std::string error;
    if( !espi_test::LoadFixture( VectorPath( name ), &frames, &error ) )
    {
        std::fprintf( stderr, "FAIL  %s\n", error.c_str() );
        TEST_CHECK( false );
        return {};
    }
    if( frames.empty() )
    {
        std::fprintf( stderr, "FAIL  %s contains no transactions\n", name );
        TEST_CHECK( false );
        return {};
    }

    std::string out;
    for( size_t i = 0; i < frames.size(); ++i )
    {
        FixtureByteSource source( frames[ i ] );
        LinkDecoder decoder( &source );
        Transaction txn;
        if( !decoder.Decode( &txn ) )
        {
            std::fprintf( stderr, "FAIL  %s transaction %zu produced nothing\n", name, i );
            TEST_CHECK( false );
            continue;
        }

        if( i != 0 )
            out += "---\n";
        out += Render( txn );

        if( !strict_consumption )
            continue;

        // The fixture says where the command ends; the decoder works it out
        // from the opcode and the shape table. Disagreement in either
        // direction is a bug, and only this check catches an under-read.
        if( !source.CommandFullyConsumed() )
            std::fprintf( stderr, "FAIL  %s: %zu command byte(s) left unread\n", name, source.CommandBytesLeft() );
        TEST_CHECK( source.CommandFullyConsumed() );

        if( !source.ResponseFullyConsumed() )
            std::fprintf( stderr, "FAIL  %s: %zu response byte(s) left unread\n", name, source.ResponseBytesLeft() );
        TEST_CHECK( source.ResponseFullyConsumed() );

        TEST_CHECK( source.TurnedAround() );
    }
    return out;
}

void CheckAgainstExpected( const char* fixture, const char* expected_name, bool strict_consumption )
{
    const std::string got = DecodeFixture( fixture, strict_consumption );

    bool ok = false;
    const std::string want = ReadFile( VectorPath( expected_name ), &ok );
    if( !ok )
    {
        std::fprintf( stderr, "FAIL  cannot read %s\n", expected_name );
        TEST_CHECK( false );
        return;
    }

    if( got != want )
        std::fprintf( stderr, "FAIL  %s decode differs from %s\n--- expected ---\n%s--- got ---\n%s-----------\n", fixture,
                      expected_name, want.c_str(), got.c_str() );
    TEST_CHECK( got == want );
}

// --- the four capture-backed and hand-built decodes -----------------------

void TestCaptureTransactions()
{
    CheckAgainstExpected( "get_configuration.espi", "get_configuration.expected", true );
    CheckAgainstExpected( "set_configuration.espi", "set_configuration.expected", true );
    CheckAgainstExpected( "get_vwire.espi", "get_vwire.expected", true );
    CheckAgainstExpected( "config_oob_channel.espi", "config_oob_channel.expected", true );
}

// Offset 08h decides how the bus itself is read -- I/O mode and CRC enable --
// and is the widest register in the map, so it is the one that exercises every
// field kind. The capture never touches it, hence a hand-built fixture.
void TestGeneralCapabilities()
{
    CheckAgainstExpected( "config_general.espi", "config_general.expected", true );

    // Both virtual wire count fields at 3Fh. The capture only ever shows 0 and
    // 7, which fit in three bits, so a field declared one bit too narrow
    // decoded every real transaction correctly. The mutation runner found that
    // gap; this fixture closes it.
    CheckAgainstExpected( "config_vwire_max.espi", "config_vwire_max.expected", true );
}

// The response modifier decides how many bytes the response phase holds, so a
// wrong modifier encoding misplaces the CRC rather than merely mislabelling a
// field. Nothing in the capture exercises it -- this is why the fixture had to
// be hand built.
void TestResponseModifierAppend()
{
    CheckAgainstExpected( "get_status_vwire_append.espi", "get_status_vwire_append.expected", true );
}

void TestWaitState()
{
    // The response CRC in this fixture covers 08 0F 01 only. If either 0Fh
    // wait state were folded in, the CRC field would render BAD and the diff
    // would fail.
    CheckAgainstExpected( "wait_state.espi", "wait_state.expected", true );
}

void TestMalformed()
{
    CheckAgainstExpected( "malformed.espi", "malformed.expected", false );
}

// --- properties that a rendered diff does not pin -------------------------

// Rule R1 depends on the fixture loader actually rejecting what it cannot
// parse. A loader that skipped bad lines would silently shrink a fixture and
// the test above would still pass.
void TestFixtureLoaderRejectsGarbage()
{
    std::vector<Frame> frames;
    std::string error;

    TEST_CHECK( !espi_test::LoadFixture( VectorPath( "does_not_exist.espi" ), &frames, &error ) );
    TEST_CHECK( !error.empty() );
}

// Every command phase is opcode ... CRC and every response phase is response
// ... CRC. The shape table states only what sits between, so that invariant is
// asserted here once rather than repeated on every table row.
void TestFramingInvariant()
{
    struct Case
    {
        uint8_t opcode;
        size_t command_bytes;  // total, including opcode and CRC
        size_t response_bytes; // total, including response byte and CRC
    };
    // Byte counts read off Figures 20/22/23 and the capture, not computed.
    const Case cases[] = {
        { 0x21, 4, 8 }, // GET_CONFIGURATION: 21 00 20 C8 / 08 00 0B 00 00 0F 01 91
        { 0x22, 8, 4 }, // SET_CONFIGURATION: 22 00 20 01 00 07 00 01 / 08 0F 01 95
        { 0x25, 2, 4 }, // GET_STATUS:        25 FB / 08 0F 01 95
    };

    for( const Case& c : cases )
    {
        PacketShape shape;
        TEST_CHECK( LookupShape( c.opcode, &shape ) );

        size_t command = 1 + 1; // opcode + CRC
        for( uint8_t i = 0; i < shape.command.count; ++i )
            command += ElementFixedSize( shape.command.items[ i ] );
        TEST_CHECK_EQ( command, c.command_bytes );

        size_t response = 1 + 1; // response byte + CRC
        for( uint8_t i = 0; i < shape.response.count; ++i )
            response += ElementFixedSize( shape.response.items[ i ] );
        TEST_CHECK_EQ( response, c.response_bytes );
    }
}

// An opcode with no transcribed shape must not resolve. This is what keeps a
// gap a gap instead of a guess.
void TestUntranscribedShapesAreGaps()
{
    const uint8_t no_shape[] = {
        0x00, // PUT_PC        -- needs cycle-type headers
        0x01, // GET_PC
        0x02, // PUT_NP
        0x03, // GET_NP
        0x06, // PUT_OOB
        0x08, // PUT_FLASH_C
        0x40, // PUT_IORD_SHORT
        0xFF, // RESET
    };
    for( uint8_t opcode : no_shape )
    {
        if( LookupShape( opcode, nullptr ) )
            std::fprintf( stderr, "FAIL  opcode 0x%02X resolved to a shape that was never transcribed\n", opcode );
        TEST_CHECK( !LookupShape( opcode, nullptr ) );
    }
}

// Table 21, p.93, typed from the rendered page. An address the map does not
// define, or one whose layout has not been transcribed, must not resolve --
// otherwise the decoder names bits in a register nobody has read.
void TestConfigRegisterGaps()
{
    struct Expect
    {
        uint16_t address;
        const char* name;
    };
    const Expect known[] = {
        { 0x0004, "Device Identification" },
        { 0x0008, "General Capabilities and Configurations" },
        { 0x0010, "Channel 0 Capabilities and Configurations" },
        { 0x0020, "Channel 1 Capabilities and Configurations" },
        { 0x0030, "Channel 2 Capabilities and Configurations" },
        { 0x0040, "Channel 3 Capabilities and Configurations" },
    };
    for( const Expect& e : known )
    {
        const char* name = nullptr;
        if( !LookupConfigRegister( e.address, &name ) )
        {
            std::fprintf( stderr, "FAIL  register 0x%04X not recognised\n", e.address );
            TEST_CHECK( false );
            continue;
        }
        TEST_CHECK( std::string( name ) == e.name );
    }

    // Table 21 names these, but nobody has transcribed their bits. "A register
    // we have not done yet" is a different answer from "not a register".
    const uint16_t no_layout[] = { 0x0044, 0x0048, 0x004C };
    for( uint16_t address : no_layout )
    {
        const char* name = nullptr;
        if( ClassifyConfigAddress( address, &name ) != ConfigAddress::NoFieldLayout )
            std::fprintf( stderr, "FAIL  address 0x%04X should be a named register with no layout\n", address );
        TEST_CHECK( ClassifyConfigAddress( address, &name ) == ConfigAddress::NoFieldLayout );
        TEST_CHECK( !LookupConfigRegister( address, nullptr ) );
    }

    const uint16_t reserved[] = {
        0x0000, // Reserved, Table 21
        0x000C, // Reserved
        0x0014, // Reserved, and 014h-01Fh is a range rather than one DWord
        0x0018, // still inside 014h-01Fh
        0x0050, // Reserved, 050h-7FFh
        0x0800, // Platform specific, 800h-FFFh
        0x0FFC, // the last DWord of the platform range
    };
    for( uint16_t address : reserved )
    {
        if( ClassifyConfigAddress( address, nullptr ) != ConfigAddress::ReservedRange )
            std::fprintf( stderr, "FAIL  address 0x%04X should classify as reserved\n", address );
        TEST_CHECK( ClassifyConfigAddress( address, nullptr ) == ConfigAddress::ReservedRange );
        TEST_CHECK( !LookupConfigRegister( address, nullptr ) );
    }

    // §3.7, p.38: "The 4 MSB address bits must be driven to all zeros by eSPI
    // controller. eSPI targets should ignore the 4 MSB address bits."
    //
    // Because the target ignores them, F020h still reaches the Channel 1
    // register -- which is precisely why the decoder must NOT quietly mask
    // them off and call it 0020h. One of those addresses is malformed and
    // happens to work, and an analyzer that hides that is hiding a real bug.
    TEST_CHECK( ClassifyConfigAddress( 0xF020, nullptr ) == ConfigAddress::UpperBitsSet );
    TEST_CHECK( ClassifyConfigAddress( 0x1020, nullptr ) == ConfigAddress::UpperBitsSet );
    TEST_CHECK( !LookupConfigRegister( 0xF020, nullptr ) );

    // A malformed address still reports which register it was reaching for.
    const char* reached = nullptr;
    ClassifyConfigAddress( 0xF020, &reached );
    TEST_CHECK( reached != nullptr && std::string( reached ) == "Channel 1 Capabilities and Configurations" );

    // §3.7, p.38: "address bit[1:0] hard-wired to always 00".
    TEST_CHECK( ClassifyConfigAddress( 0x0021, nullptr ) == ConfigAddress::NotDwordAligned );
    TEST_CHECK( ClassifyConfigAddress( 0x0022, nullptr ) == ConfigAddress::NotDwordAligned );
    TEST_CHECK( ClassifyConfigAddress( 0x0006, nullptr ) == ConfigAddress::NotDwordAligned );

    // An unaligned address inside a four-byte register range still names it.
    // This is what makes Table 21's End column observable at all: only the
    // base of such a range is ever a legal address, so nothing else reads the
    // End, and a wrong End would sit there undetected.
    const char* inside = nullptr;
    ClassifyConfigAddress( 0x0007, &inside );
    TEST_CHECK( inside != nullptr && std::string( inside ) == "Device Identification" );
    ClassifyConfigAddress( 0x0006, &inside );
    TEST_CHECK( inside != nullptr && std::string( inside ) == "Device Identification" );
    ClassifyConfigAddress( 0x0022, &inside );
    TEST_CHECK( inside != nullptr && std::string( inside ) == "Channel 1 Capabilities and Configurations" );

    // The malformedness checks come first: a malformed address is reported as
    // malformed even when it would otherwise land on a reserved range.
    TEST_CHECK( ClassifyConfigAddress( 0xF050, nullptr ) == ConfigAddress::UpperBitsSet );
}

// Table 21 gives every entry a Start and an End -- Device Identification is
// 004h through 007h, not 004h alone. The reserved spans between the registers
// are ranges too, and a range transcribed with the wrong end swallows the
// register that follows it.
void TestConfigRegisterRanges()
{
    struct Span
    {
        uint16_t start;
        uint16_t end;
        const char* name;
    };
    // Typed from Table 21, p.93, Start (Hex) and End (Hex) columns.
    const Span table[] = {
        { 0x000, 0x003, "Reserved" },
        { 0x004, 0x007, "Device Identification" },
        { 0x008, 0x00B, "General Capabilities and Configurations" },
        { 0x00C, 0x00F, "Reserved" },
        { 0x010, 0x013, "Channel 0 Capabilities and Configurations" },
        { 0x014, 0x01F, "Reserved" },
        { 0x020, 0x023, "Channel 1 Capabilities and Configurations" },
        { 0x024, 0x02F, "Reserved" },
        { 0x030, 0x033, "Channel 2 Capabilities and Configurations" },
        { 0x034, 0x03F, "Reserved" },
        { 0x040, 0x043, "Channel 3 Capabilities and Configurations" },
        { 0x044, 0x047, "Channel 3 Capabilities and Configurations 2" },
        { 0x048, 0x04B, "Channel 3 Capabilities and Configurations 3" },
        { 0x04C, 0x04F, "Channel 3 Capabilities and Configurations 4" },
        { 0x050, 0x7FF, "Reserved" },
        { 0x800, 0xFFF, "Platform Specific registers" },
    };

    // Every address in a span must report that span's name -- every address,
    // not just the DWord-aligned ones, because the unaligned ones are what pin
    // the End column.
    for( const Span& s : table )
    {
        for( uint16_t a = s.start; a <= s.end; ++a )
        {
            const char* name = nullptr;
            ClassifyConfigAddress( a, &name );
            if( name == nullptr || std::string( name ) != s.name )
                std::fprintf( stderr, "FAIL  address 0x%03X: expected %s got %s\n", a, s.name,
                              name != nullptr ? name : "(none)" );
            TEST_CHECK( name != nullptr && std::string( name ) == s.name );
        }

        const uint16_t past = static_cast<uint16_t>( s.end + 1 );
        if( past <= 0xFFF )
        {
            const char* name = nullptr;
            ClassifyConfigAddress( past, &name );
            if( name != nullptr && std::string( name ) == s.name )
                std::fprintf( stderr, "FAIL  span %s runs past its end at 0x%03X\n", s.name, past );
        }
    }
}

// Access type and default for every field, typed from the Type and Default
// columns of §6.2, pp.94-103.
//
// The Default column is not documentation. It is where the state machine
// starts: a capture opens with the link out of eSPI Reset#, and nothing on the
// wire says what mode the bus is in, so the first transaction is decoded from
// these values or not at all.
void TestConfigFieldTypes()
{
    struct Expect
    {
        uint16_t address;
        uint8_t high;
        uint8_t low;
        ConfigAccess access;
        ConfigDefault kind;
        uint32_t value;
    };
    const Expect table[] = {
        // 004h, p.94
        { 0x004, 7, 0, ConfigAccess::RO, ConfigDefault::Value, 0x01 },
        // 008h, pp.94-96. Note 25:24 and 23 have an empty Default column.
        { 0x008, 31, 31, ConfigAccess::RW, ConfigDefault::Value, 0 },
        { 0x008, 30, 30, ConfigAccess::RW, ConfigDefault::Value, 0 },
        { 0x008, 29, 29, ConfigAccess::RO, ConfigDefault::HwInit, 0 },
        { 0x008, 28, 28, ConfigAccess::RW, ConfigDefault::Value, 0 },
        { 0x008, 27, 26, ConfigAccess::RW, ConfigDefault::Value, 0 },
        { 0x008, 25, 24, ConfigAccess::RO, ConfigDefault::None, 0 },
        { 0x008, 23, 23, ConfigAccess::RW, ConfigDefault::None, 0 },
        { 0x008, 22, 20, ConfigAccess::RW, ConfigDefault::Value, 0 },
        { 0x008, 19, 19, ConfigAccess::RO, ConfigDefault::HwInit, 0 },
        { 0x008, 18, 16, ConfigAccess::RO, ConfigDefault::HwInit, 0 },
        { 0x008, 15, 12, ConfigAccess::RW, ConfigDefault::Value, 0 },
        { 0x008, 7, 0, ConfigAccess::RO, ConfigDefault::HwInit, 0 },
        // 010h, pp.97-98
        { 0x010, 14, 12, ConfigAccess::RW, ConfigDefault::Value, 1 },
        { 0x010, 10, 8, ConfigAccess::RW, ConfigDefault::Value, 1 },
        { 0x010, 6, 4, ConfigAccess::RO, ConfigDefault::HwInit, 0 },
        { 0x010, 2, 2, ConfigAccess::RW, ConfigDefault::Value, 0 },
        { 0x010, 1, 1, ConfigAccess::RO, ConfigDefault::Value, 0 },
        { 0x010, 0, 0, ConfigAccess::RW, ConfigDefault::Value, 1 },
        // 020h, p.99
        { 0x020, 21, 16, ConfigAccess::RW, ConfigDefault::Value, 0 },
        { 0x020, 13, 8, ConfigAccess::RO, ConfigDefault::HwInit, 0 },
        { 0x020, 1, 1, ConfigAccess::RO, ConfigDefault::Value, 0 },
        { 0x020, 0, 0, ConfigAccess::RW, ConfigDefault::Value, 0 },
        // 030h, p.100
        { 0x030, 10, 8, ConfigAccess::RW, ConfigDefault::Value, 1 },
        { 0x030, 6, 4, ConfigAccess::RO, ConfigDefault::HwInit, 0 },
        { 0x030, 1, 1, ConfigAccess::RO, ConfigDefault::Value, 0 },
        { 0x030, 0, 0, ConfigAccess::RW, ConfigDefault::Value, 0 },
        // 040h, pp.101-103
        { 0x040, 31, 24, ConfigAccess::RO, ConfigDefault::HwInit, 0 },
        { 0x040, 23, 20, ConfigAccess::RO, ConfigDefault::HwInit, 0 },
        { 0x040, 17, 16, ConfigAccess::RO, ConfigDefault::HwInit, 0 },
        { 0x040, 14, 12, ConfigAccess::RW, ConfigDefault::Value, 1 },
        { 0x040, 11, 11, ConfigAccess::RwOrRo, ConfigDefault::HwInit, 0 },
        { 0x040, 10, 8, ConfigAccess::RW, ConfigDefault::Value, 1 },
        { 0x040, 7, 5, ConfigAccess::RO, ConfigDefault::HwInit, 0 },
        // The page prints "01b" for this three bit field. Read as 001b.
        { 0x040, 4, 2, ConfigAccess::RW, ConfigDefault::Value, 1 },
        { 0x040, 1, 1, ConfigAccess::RO, ConfigDefault::Value, 0 },
        { 0x040, 0, 0, ConfigAccess::RW, ConfigDefault::Value, 0 },
    };

    for( const Expect& e : table )
    {
        ConfigField fields[ 16 ];
        const size_t count = DecodeConfigRegister( e.address, 0, fields, 16 );
        bool found = false;
        for( size_t i = 0; i < count && i < 16; ++i )
        {
            const ConfigField& f = fields[ i ];
            if( f.high != e.high || f.low != e.low )
                continue;
            found = true;
            if( f.access != e.access || f.default_kind != e.kind
                || ( e.kind == ConfigDefault::Value && f.default_value != e.value ) )
                std::fprintf( stderr, "FAIL  %03X bits %u:%u (%s): type or default differs\n", e.address, e.high, e.low,
                              f.name );
            TEST_CHECK( f.access == e.access );
            TEST_CHECK( f.default_kind == e.kind );
            if( e.kind == ConfigDefault::Value )
                TEST_CHECK_EQ( f.default_value, e.value );
        }
        if( !found )
            std::fprintf( stderr, "FAIL  %03X has no field at bits %u:%u\n", e.address, e.high, e.low );
        TEST_CHECK( found );
    }
}

// The assembled reset state, which is what a decoder must assume for the very
// first transaction of a capture.
void TestConfigResetState()
{
    uint32_t value = 0;
    uint32_t known = 0;

    // 008h. The two fields that decide how the bus is read: I/O Mode Select
    // 00b is Single I/O, and CRC Checking Enable 0 means the CRC byte is
    // present but not checked. Both matter before a single byte is decoded.
    TEST_CHECK( ConfigResetValue( 0x008, &value, &known ) );
    TEST_CHECK_EQ( ( value >> 26 ) & 0x3u, 0u ); // Single I/O
    TEST_CHECK_EQ( ( value >> 31 ) & 0x1u, 0u ); // CRC checking disabled
    TEST_CHECK_EQ( ( value >> 12 ) & 0xFu, 0u ); // 16 byte times of wait state
    // I/O Mode Support, Open Drain Alert# Select and the HwInit fields have no
    // spec default, so the reset state cannot claim to know them.
    TEST_CHECK_EQ( ( known >> 24 ) & 0x3u, 0u ); // I/O Mode Support: unknown
    TEST_CHECK_EQ( ( known >> 23 ) & 0x1u, 0u ); // Open Drain Alert# Select
    TEST_CHECK_EQ( ( known >> 16 ) & 0x7u, 0u ); // Maximum Frequency Supported
    TEST_CHECK_EQ( known & 0xFFu, 0u );          // Channel Supported
    TEST_CHECK_EQ( ( known >> 31 ) & 0x1u, 1u ); // but CRC enable is known

    // 010h. The peripheral channel is the only one enabled out of reset.
    TEST_CHECK( ConfigResetValue( 0x010, &value, &known ) );
    TEST_CHECK_EQ( value & 0x1u, 1u );           // Peripheral Channel Enable
    TEST_CHECK_EQ( ( value >> 1 ) & 0x1u, 0u );  // not yet Ready
    TEST_CHECK_EQ( ( value >> 8 ) & 0x7u, 1u );  // 64 byte payload
    TEST_CHECK_EQ( ( value >> 12 ) & 0x7u, 1u ); // 64 byte read request

    // 020h, 030h and 040h all come up disabled.
    for( uint16_t address : { uint16_t( 0x020 ), uint16_t( 0x030 ), uint16_t( 0x040 ) } )
    {
        TEST_CHECK( ConfigResetValue( address, &value, &known ) );
        if( ( value & 0x1u ) != 0u )
            std::fprintf( stderr, "FAIL  register %03X comes up enabled\n", address );
        TEST_CHECK_EQ( value & 0x1u, 0u );
        TEST_CHECK_EQ( known & 0x1u, 1u ); // and that is a stated default
    }

    // 004h advertises this revision of the specification.
    TEST_CHECK( ConfigResetValue( 0x004, &value, &known ) );
    TEST_CHECK_EQ( value & 0xFFu, 0x01u );

    // A register with no transcribed layout has no reset state to offer.
    TEST_CHECK( !ConfigResetValue( 0x044, &value, &known ) );
    TEST_CHECK( !ConfigResetValue( 0x050, &value, &known ) );
}

// Table 3, p.30, typed from the rendered page.
void TestResponseEncodings()
{
    struct Expect
    {
        uint8_t byte;
        const char* name;
    };
    const Expect table[] = {
        { 0x08, "ACCEPT" }, { 0x01, "DEFER" },      { 0x02, "NON_FATAL_ERROR" },
        { 0x03, "FATAL_ERROR" }, { 0x0F, "WAIT_STATE" }, { 0xFF, "NO_RESPONSE" },
    };

    for( const Expect& e : table )
    {
        ResponseInfo info;
        if( !LookupResponse( e.byte, &info ) )
        {
            std::fprintf( stderr, "FAIL  response 0x%02X not recognised\n", e.byte );
            TEST_CHECK( false );
            continue;
        }
        if( std::string( info.name ) != e.name )
            std::fprintf( stderr, "FAIL  response 0x%02X: expected %s got %s\n", e.byte, e.name, info.name );
        TEST_CHECK( std::string( info.name ) == e.name );
    }

    // NO_RESPONSE shares its low nibble with WAIT_STATE. Matching the nibble
    // first would classify FFh as a wait state, and a decoder would then sit
    // waiting for a response the spec says is never driven.
    TEST_CHECK( IsWaitState( 0x0F ) );
    TEST_CHECK( !IsWaitState( 0xFF ) );

    ResponseInfo info;
    TEST_CHECK( LookupResponse( 0xFF, &info ) );
    TEST_CHECK( info.code == ResponseCode::NoResponse );

    // ACCEPT with a response modifier: R1R0 in [7:6], reserved in [5:4].
    TEST_CHECK( LookupResponse( 0x88, &info ) );
    TEST_CHECK( std::string( info.name ) == "ACCEPT" );
    TEST_CHECK_EQ( info.modifier, uint8_t( 0x2 ) );
    TEST_CHECK_EQ( info.reserved, uint8_t( 0x0 ) );

    // Reserved bits set. The spec requires the target to drive them to 0, so
    // the decoder must surface this rather than mask it away.
    TEST_CHECK( LookupResponse( 0x18, &info ) );
    TEST_CHECK_EQ( info.reserved, uint8_t( 0x1 ) );

    // An encoding Table 3 does not define.
    TEST_CHECK( !LookupResponse( 0x04, nullptr ) );
    TEST_CHECK( !LookupResponse( 0x0A, nullptr ) );
}

// Table 4 and Figure 16, pp.31-33, typed from the rendered pages.
void TestStatusBits()
{
    struct Expect
    {
        const char* name;
        uint8_t bit;
    };
    const Expect table[] = {
        { "PC_FREE", 0 },      { "NP_FREE", 1 },       { "VWIRE_FREE", 2 },    { "OOB_FREE", 3 },
        { "PC_AVAIL", 4 },     { "NP_AVAIL", 5 },      { "VWIRE_AVAIL", 6 },   { "OOB_AVAIL", 7 },
        { "FLASH_C_FREE", 8 }, { "FLASH_NP_FREE", 9 }, { "FLASH_C_AVAIL", 12 }, { "FLASH_NP_AVAIL", 13 },
    };

    TEST_CHECK_EQ( StatusBitCount(), sizeof( table ) / sizeof( table[ 0 ] ) );

    for( size_t i = 0; i < StatusBitCount() && i < sizeof( table ) / sizeof( table[ 0 ] ); ++i )
    {
        const StatusBitInfo& got = StatusBitAt( i );
        if( std::string( got.name ) != table[ i ].name || got.bit != table[ i ].bit )
            std::fprintf( stderr, "FAIL  status bit %zu: expected %s at %u, got %s at %u\n", i, table[ i ].name,
                          table[ i ].bit, got.name, got.bit );
        TEST_CHECK( std::string( got.name ) == table[ i ].name );
        TEST_CHECK_EQ( got.bit, table[ i ].bit );
    }

    // Bits 11:10 and 15:14. The stored description of Figure 16 puts the flash
    // bits at 15:12, which would make this mask 0x0C00.
    TEST_CHECK_EQ( StatusReservedMask(), uint16_t( 0xCC00 ) );
}

} // namespace

int main()
{
    TestCaptureTransactions();
    TestGeneralCapabilities();
    TestConfigRegisterGaps();
    TestConfigRegisterRanges();
    TestConfigFieldTypes();
    TestConfigResetState();
    TestResponseModifierAppend();
    TestWaitState();
    TestMalformed();
    TestFixtureLoaderRejectsGarbage();
    TestFramingInvariant();
    TestUntranscribedShapesAreGaps();
    TestResponseEncodings();
    TestStatusBits();
    TEST_MAIN_RETURN();
}
