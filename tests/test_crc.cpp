// T0 -- CRC-8 primitives.
//
// Every vector below is literal: the byte sequence and its expected CRC are
// both transcribed from tests/vectors/espi_dump.txt, a capture produced by a
// third-party decoder against real hardware. Nothing here is computed by our
// own CRC code and then compared against itself (rules R1/R2).
//
// This is the strongest independent evidence in the suite. It pins three
// things at once that a same-author reimplementation would not:
//   - the polynomial, seed, bit order and absence of reflection
//   - that the command-phase CRC spans opcode + header + data
//   - that the response-phase CRC spans response + data + STATUS
//
// The last one matters most. Getting the algorithm right but the covered span
// wrong is the realistic failure mode, and only real bus traffic catches it.

#include "espi/Crc8.h"
#include "support/TestMacros.h"

#include <vector>

using espi::Crc8;

namespace
{

struct Vector
{
    const char* name;
    std::vector<uint8_t> bytes;
    uint8_t expected;
};

// ---------------------------------------------------------------------------
//  Transcribed from tests/vectors/espi_dump.txt.
//
//  Command-phase spans start at the opcode; response-phase spans start at the
//  response code and run through both STATUS bytes, stopping before the CRC.
// ---------------------------------------------------------------------------
const std::vector<Vector> kCaptureVectors = {
    // --- command phase: opcode + header (+ data) ---
    { "cmd GET_CONFIGURATION 0x0020", { 0x21, 0x00, 0x20 }, 0xC8 },
    { "cmd GET_CONFIGURATION 0x0030", { 0x21, 0x00, 0x30 }, 0xB8 },
    { "cmd GET_VWIRE", { 0x05 }, 0x1B },
    { "cmd SET_CONFIGURATION 0x0020", { 0x22, 0x00, 0x20, 0x01, 0x00, 0x07, 0x00 }, 0x01 },

    // --- response phase: response + data + status ---
    { "rsp ACCEPT cfg 0x00000B00", { 0x08, 0x00, 0x0B, 0x00, 0x00, 0x0F, 0x01 }, 0x91 },
    { "rsp ACCEPT cfg 0x00070B01", { 0x08, 0x01, 0x0B, 0x07, 0x00, 0x0F, 0x01 }, 0xDA },
    { "rsp ACCEPT cfg 0x00070B03", { 0x08, 0x03, 0x0B, 0x07, 0x00, 0x4F, 0x01 }, 0xD3 },
    { "rsp ACCEPT cfg 0x00000111", { 0x08, 0x11, 0x01, 0x00, 0x00, 0x0F, 0x01 }, 0xFB },
    { "rsp ACCEPT cfg 0x00000113", { 0x08, 0x13, 0x01, 0x00, 0x00, 0x0F, 0x01 }, 0xA9 },
    { "rsp ACCEPT no data", { 0x08, 0x0F, 0x01 }, 0x95 },
};

void TestCaptureVectors()
{
    for( const Vector& v : kCaptureVectors )
    {
        const uint8_t got = Crc8::Compute( v.bytes.data(), v.bytes.size() );
        if( got != v.expected )
            std::fprintf( stderr, "  vector: %s\n", v.name );
        TEST_CHECK_EQ( got, v.expected );
    }
}

// Structural properties from the spec text (section 5.2), independent of the
// capture: these would catch a seed or reset bug that happened to be masked by
// the vectors above.
void TestSpecProperties()
{
    Crc8 crc;
    TEST_CHECK_EQ( crc.Value(), uint8_t( 0x00 ) ); // seed is 00h

    // Reset restores the seed.
    crc.Update( 0xA5 );
    crc.Reset();
    TEST_CHECK_EQ( crc.Value(), uint8_t( 0x00 ) );

    // Incremental accumulation must equal a one-shot over the same bytes --
    // the decoder accumulates byte by byte as it walks the waveform, while
    // these vectors compute in one call.
    const std::vector<uint8_t> bytes = { 0x21, 0x00, 0x20 };
    Crc8 incremental;
    for( uint8_t b : bytes )
        incremental.Update( b );
    TEST_CHECK_EQ( incremental.Value(), Crc8::Compute( bytes.data(), bytes.size() ) );

    // A single 0x00 byte through a 00h seed stays 0x00; the first nonzero byte
    // must move it. Cheap guard against a no-op Update().
    TEST_CHECK_EQ( Crc8::Compute( { 0x00 } ), uint8_t( 0x00 ) );
    TEST_CHECK( Crc8::Compute( { 0x01 } ) != 0x00 );

    // Order dependence: a CRC that ignored position would pass many vectors.
    TEST_CHECK( Crc8::Compute( { 0x21, 0x00, 0x20 } ) != Crc8::Compute( { 0x20, 0x00, 0x21 } ) );
}

} // namespace

int main()
{
    TestCaptureVectors();
    TestSpecProperties();
    TEST_MAIN_RETURN();
}
