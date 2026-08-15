// T1 -- command opcode encodings.
//
// Every expectation below is a literal written from the rendered pages of
// Table 2 (base spec pp.25-27). The test cannot reach the table it checks:
// src/core/tables/ is private to espi_core (rule R3), so these strings and
// values were typed from the specification, not lifted from the header.
//
// What this catches: a table that drifts from what its author read.
// What this cannot catch: an author who misread the page. Only a second person
// reading the rendered page closes that, which is what tools/qc_worksheet.py
// produces a checklist for.
//
// Three of these opcodes -- 0x21, 0x22, 0x05 -- also appear in the third-party
// capture, so they carry independent corroboration on top of the transcription.

#include "espi/Opcodes.h"
#include "support/TestMacros.h"

#include <cstring>
#include <string>
#include <vector>

using namespace espi;

namespace
{

struct Expect
{
    uint8_t encoding;
    const char* name;
    ChannelId channel;
};

// Table 2, in page order.
const std::vector<Expect> kTable = {
    // eSPI Peripheral Channel, p.25
    { 0x00, "PUT_PC", ChannelId::Peripheral },
    { 0x01, "GET_PC", ChannelId::Peripheral },
    { 0x02, "PUT_NP", ChannelId::Peripheral },
    { 0x03, "GET_NP", ChannelId::Peripheral },
    // Virtual Wire Channel, p.26
    { 0x04, "PUT_VWIRE", ChannelId::VirtualWire },
    { 0x05, "GET_VWIRE", ChannelId::VirtualWire }, // corroborated by the capture
    // OOB Message Channel, p.26
    { 0x06, "PUT_OOB", ChannelId::Oob },
    { 0x07, "GET_OOB", ChannelId::Oob },
    // Flash Access Channel, p.26-27
    { 0x08, "PUT_FLASH_C", ChannelId::Flash },
    { 0x09, "GET_FLASH_NP", ChannelId::Flash },
    { 0x0A, "PUT_FLASH_NP", ChannelId::Flash },
    { 0x0B, "GET_FLASH_C", ChannelId::Flash },
    // Channel Independent, p.27
    { 0x21, "GET_CONFIGURATION", ChannelId::ChannelIndependent }, // corroborated
    { 0x22, "SET_CONFIGURATION", ChannelId::ChannelIndependent }, // corroborated
    { 0x25, "GET_STATUS", ChannelId::ChannelIndependent },
    { 0xFF, "RESET", ChannelId::ChannelIndependent },
};

void TestExactEncodings()
{
    for( const Expect& e : kTable )
    {
        OpcodeInfo info;
        if( !LookupOpcode( e.encoding, &info ) )
        {
            std::fprintf( stderr, "FAIL  opcode 0x%02X (%s) not recognised\n", e.encoding, e.name );
            TEST_CHECK( false );
            continue;
        }
        if( std::strcmp( info.name, e.name ) != 0 )
            std::fprintf( stderr, "FAIL  opcode 0x%02X: expected %s got %s\n", e.encoding, e.name, info.name );
        TEST_CHECK( std::strcmp( info.name, e.name ) == 0 );
        TEST_CHECK( info.channel == e.channel );
        TEST_CHECK( !info.has_short_length );
    }
}

// The short cycles occupy 010000CC / 010001CC / 010010CC / 010011CC.
// Base encodings are 0x40, 0x44, 0x48, 0x4C with C1C0 in the low two bits.
void TestShortCycleEncodings()
{
    struct ShortExpect
    {
        uint8_t base;
        const char* name;
    };
    const std::vector<ShortExpect> shorts = {
        { 0x40, "PUT_IORD_SHORT" },
        { 0x44, "PUT_IOWR_SHORT" },
        { 0x48, "PUT_MEMRD32_SHORT" },
        { 0x4C, "PUT_MEMWR32_SHORT" },
    };

    // Note 1, p.27: 00 -> 1 byte, 01 -> 2 bytes, 10 -> Reserved, 11 -> 4 bytes.
    const uint8_t expected_length[ 4 ] = { 1, 2, 0, 4 };

    for( const ShortExpect& s : shorts )
        for( uint8_t c1c0 = 0; c1c0 < 4; ++c1c0 )
        {
            const uint8_t encoding = static_cast<uint8_t>( s.base | c1c0 );
            OpcodeInfo info;
            if( !LookupOpcode( encoding, &info ) )
            {
                std::fprintf( stderr, "FAIL  short opcode 0x%02X (%s, C1C0=%u) not recognised\n", encoding, s.name, c1c0 );
                TEST_CHECK( false );
                continue;
            }
            if( std::strcmp( info.name, s.name ) != 0 )
                std::fprintf( stderr, "FAIL  0x%02X: expected %s got %s\n", encoding, s.name, info.name );
            TEST_CHECK( std::strcmp( info.name, s.name ) == 0 );
            TEST_CHECK( info.has_short_length );
            TEST_CHECK_EQ( info.request_length, expected_length[ c1c0 ] );
            TEST_CHECK( info.length_reserved == ( c1c0 == 0x2 ) );
        }

    TEST_CHECK_EQ( ShortRequestLength( 0x0 ), uint8_t( 1 ) );
    TEST_CHECK_EQ( ShortRequestLength( 0x1 ), uint8_t( 2 ) );
    TEST_CHECK_EQ( ShortRequestLength( 0x2 ), uint8_t( 0 ) ); // Reserved
    TEST_CHECK_EQ( ShortRequestLength( 0x3 ), uint8_t( 4 ) );
}

// Guard against the nine-bit misreading: if someone re-transcribes the short
// cycles from extracted text, the encodings shift and these bytes stop
// resolving to the names above. 0x41 must be PUT_IORD_SHORT with a 2-byte
// request, not some other opcode.
void TestShortCycleFootnoteHazard()
{
    OpcodeInfo info;
    TEST_CHECK( LookupOpcode( 0x41, &info ) );
    TEST_CHECK( std::strcmp( info.name, "PUT_IORD_SHORT" ) == 0 );
    TEST_CHECK_EQ( info.request_length, uint8_t( 2 ) );

    TEST_CHECK( LookupOpcode( 0x4F, &info ) );
    TEST_CHECK( std::strcmp( info.name, "PUT_MEMWR32_SHORT" ) == 0 );
    TEST_CHECK_EQ( info.request_length, uint8_t( 4 ) );
}

// Encodings Table 2 does not define must not resolve. A decoder that invents a
// name for an undefined opcode hides malformed traffic.
void TestUndefinedEncodingsRejected()
{
    const std::vector<uint8_t> undefined = { 0x0C, 0x0F, 0x10, 0x20, 0x23, 0x24, 0x26, 0x3F, 0x50, 0x80, 0xFE };
    for( uint8_t e : undefined )
    {
        OpcodeInfo info;
        if( LookupOpcode( e, &info ) )
            std::fprintf( stderr, "FAIL  undefined opcode 0x%02X resolved to %s\n", e, info.name );
        TEST_CHECK( !LookupOpcode( e, nullptr ) );
    }
}

// Exhaustive: exactly 16 exact encodings plus 4 short families x 4 lengths.
void TestTotalCoverage()
{
    int recognised = 0;
    for( int b = 0; b <= 0xFF; ++b )
        if( LookupOpcode( static_cast<uint8_t>( b ), nullptr ) )
            ++recognised;

    TEST_CHECK_EQ( recognised, 16 + 16 );
}

} // namespace

int main()
{
    TestExactEncodings();
    TestShortCycleEncodings();
    TestShortCycleFootnoteHazard();
    TestUndefinedEncodingsRejected();
    TestTotalCoverage();
    TEST_MAIN_RETURN();
}
