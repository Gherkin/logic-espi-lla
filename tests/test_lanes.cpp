// T0 -- bit/lane conversion for Single, Dual and Quad I/O.
//
// Lane packing is the one fact the decoder and the test serializer must share
// (see LaneCodec.h). So it does NOT get a round-trip test as its primary
// evidence -- Pack and Unpack are each checked against the same literal lane
// tables, transcribed by hand from the rendered waveform diagrams:
//
//     Figure 56: Single I/O Mode   base spec p.88
//     Figure 57: Dual I/O Mode     base spec p.88
//     Figure 58: Quad I/O Mode     base spec p.89
//
// Because both directions are compared to the figure rather than to each
// other, a wrong lane order fails both independently. A round-trip test is
// included at the end, but only to catch asymmetry -- it proves nothing on
// its own and is labelled as such.
//
// NOTATION. Each clock is four characters, one per physical lane, lane 0
// leftmost:
//
//     "0100"   ->  IO[0]=0  IO[1]=1  IO[2]=0  IO[3]=0
//     "01.."   ->  IO[0]=0  IO[1]=1, lanes 2 and 3 not driven
//
// Read the figures with this in mind: they stack IO[0] at the top, so the
// leftmost character here is the topmost trace there.

#include "espi/LaneCodec.h"
#include "support/TestMacros.h"

#include <string>
#include <vector>

using namespace espi;

namespace
{

struct LaneCase
{
    const char* name;
    IoMode mode;
    Phase phase;
    uint8_t value;
    std::vector<std::string> clocks;
};

// ---------------------------------------------------------------------------
//  Transcribed from Figures 56/57/58.
//
//  0x21 = 0b0010_0001   (GET_CONFIGURATION, so it also appears in the capture)
//  0xB4 = 0b1011_0100   (chosen for bit variety: no two adjacent lanes equal
//                        in Quad, which would mask a lane swap)
// ---------------------------------------------------------------------------
const std::vector<LaneCase> kFigureCases = {
    // --- Figure 56: Single I/O. Command on IO[0], MSB first, 1 bit/clock. ---
    { "single cmd 0x21", IoMode::Single, Phase::Command, 0x21,
      { "0...", "0...", "1...", "0...", "0...", "0...", "0...", "1..." } },
    { "single cmd 0xB4", IoMode::Single, Phase::Command, 0xB4,
      { "1...", "0...", "1...", "1...", "0...", "1...", "0...", "0..." } },

    // --- Figure 56, lower waveform: response comes back on IO[1], not IO[0]. ---
    { "single rsp 0x21", IoMode::Single, Phase::Response, 0x21,
      { ".0..", ".0..", ".1..", ".0..", ".0..", ".0..", ".0..", ".1.." } },

    // --- Figure 57: Dual I/O. IO[1] takes the higher bit of each pair. ---
    //     clock 1: IO[1]=b7 IO[0]=b6      clock 3: IO[1]=b3 IO[0]=b2
    //     clock 2: IO[1]=b5 IO[0]=b4      clock 4: IO[1]=b1 IO[0]=b0
    { "dual cmd 0x21", IoMode::Dual, Phase::Command, 0x21, { "00..", "01..", "00..", "10.." } },
    { "dual cmd 0xB4", IoMode::Dual, Phase::Command, 0xB4, { "01..", "11..", "10..", "00.." } },
    { "dual rsp 0xB4", IoMode::Dual, Phase::Response, 0xB4, { "01..", "11..", "10..", "00.." } },

    // --- Figure 58: Quad I/O. Upper nibble first, MSB on IO[3]. ---
    //     clock 1: IO[3]=b7 IO[2]=b6 IO[1]=b5 IO[0]=b4
    //     clock 2: IO[3]=b3 IO[2]=b2 IO[1]=b1 IO[0]=b0
    { "quad cmd 0x21", IoMode::Quad, Phase::Command, 0x21, { "0100", "1000" } },
    { "quad cmd 0xB4", IoMode::Quad, Phase::Command, 0xB4, { "1101", "0010" } },
    { "quad rsp 0x21", IoMode::Quad, Phase::Response, 0x21, { "0100", "1000" } },
};

LaneBits Parse( const std::string& s )
{
    LaneBits b{ false, false, false, false };
    for( size_t i = 0; i < 4 && i < s.size(); ++i )
        b[ i ] = ( s[ i ] == '1' );
    return b;
}

std::string Render( const LaneBits& b, LaneSet active )
{
    std::string s = "....";
    for( int k = 0; k < active.count; ++k )
        s[ active.first + k ] = b[ active.first + k ] ? '1' : '0';
    return s;
}

// Pack must reproduce the figure.
void TestPackMatchesFigures()
{
    for( const LaneCase& c : kFigureCases )
    {
        LaneBits produced[ 8 ];
        const size_t n = PackByte( c.mode, c.phase, c.value, produced, 8 );
        TEST_CHECK_EQ( n, c.clocks.size() );
        if( n != c.clocks.size() )
            continue;

        const LaneSet active = LanesFor( c.mode, c.phase );
        for( size_t i = 0; i < n; ++i )
        {
            const std::string got = Render( produced[ i ], active );
            const std::string want = c.clocks[ i ];
            if( got != want )
                std::fprintf( stderr, "FAIL  pack %s clock %zu: expected %s got %s\n", c.name, i + 1, want.c_str(),
                              got.c_str() );
            TEST_CHECK( got == want );
        }
    }
}

// Unpack must recover the byte from the figure. Independent of Pack.
void TestUnpackMatchesFigures()
{
    for( const LaneCase& c : kFigureCases )
    {
        ByteAssembler asm_( c.mode, c.phase );
        uint8_t got = 0;
        bool complete = false;

        for( size_t i = 0; i < c.clocks.size(); ++i )
        {
            const bool done = asm_.Feed( Parse( c.clocks[ i ] ), &got );
            // A byte must complete on the last clock and no earlier.
            const bool expect_done = ( i + 1 == c.clocks.size() );
            if( done != expect_done )
                std::fprintf( stderr, "FAIL  unpack %s: byte completed at clock %zu\n", c.name, i + 1 );
            TEST_CHECK( done == expect_done );
            complete = complete || done;
        }

        TEST_CHECK( complete );
        if( got != c.value )
            std::fprintf( stderr, "  case: unpack %s\n", c.name );
        TEST_CHECK_EQ( got, c.value );
    }
}

// Geometry constants, straight from the figures' clock numbering.
//   Single: command opcode+7 header+CRC = 9 bytes over clocks 1..72
//   Dual:   same 9 bytes over clocks 1..36
//   Quad:   same 9 bytes over clocks 1..18
void TestGeometry()
{
    TEST_CHECK_EQ( BitsPerClock( IoMode::Single ), 1 );
    TEST_CHECK_EQ( BitsPerClock( IoMode::Dual ), 2 );
    TEST_CHECK_EQ( BitsPerClock( IoMode::Quad ), 4 );

    TEST_CHECK_EQ( ClocksPerByte( IoMode::Single ) * 9, 72 );
    TEST_CHECK_EQ( ClocksPerByte( IoMode::Dual ) * 9, 36 );
    TEST_CHECK_EQ( ClocksPerByte( IoMode::Quad ) * 9, 18 );

    // The direction split. This is the assertion that fails if someone
    // "simplifies" Single mode to always use IO[0].
    TEST_CHECK( LanesFor( IoMode::Single, Phase::Command ) == ( LaneSet{ 0, 1 } ) );
    TEST_CHECK( LanesFor( IoMode::Single, Phase::Response ) == ( LaneSet{ 1, 1 } ) );
    TEST_CHECK( LanesFor( IoMode::Dual, Phase::Command ) == ( LaneSet{ 0, 2 } ) );
    TEST_CHECK( LanesFor( IoMode::Dual, Phase::Response ) == ( LaneSet{ 0, 2 } ) );
    TEST_CHECK( LanesFor( IoMode::Quad, Phase::Command ) == ( LaneSet{ 0, 4 } ) );
    TEST_CHECK( LanesFor( IoMode::Quad, Phase::Response ) == ( LaneSet{ 0, 4 } ) );

    TEST_CHECK_EQ( kTurnAroundClocks, 2 );
}

void TestAlignment()
{
    ByteAssembler asm_( IoMode::Quad, Phase::Command );
    TEST_CHECK( asm_.Aligned() );
    asm_.Feed( Parse( "0100" ), nullptr );
    TEST_CHECK( !asm_.Aligned() ); // mid-byte
    asm_.Reset();
    TEST_CHECK( asm_.Aligned() );
}

// Secondary only. Proves Pack and Unpack are not asymmetric; proves nothing
// about whether either matches the specification. The figure tables above are
// what establish that.
void TestRoundTripAllBytes()
{
    const IoMode modes[] = { IoMode::Single, IoMode::Dual, IoMode::Quad };
    const Phase phases[] = { Phase::Command, Phase::Response };

    for( IoMode mode : modes )
        for( Phase phase : phases )
            for( int v = 0; v <= 0xFF; ++v )
            {
                LaneBits clocks[ 8 ];
                const size_t n = PackByte( mode, phase, static_cast<uint8_t>( v ), clocks, 8 );

                ByteAssembler asm_( mode, phase );
                uint8_t got = 0;
                for( size_t i = 0; i < n; ++i )
                    asm_.Feed( clocks[ i ], &got );

                if( got != v )
                {
                    std::fprintf( stderr, "FAIL  roundtrip mode=%d phase=%d value=0x%02X got=0x%02X\n",
                                  static_cast<int>( mode ), static_cast<int>( phase ), v, got );
                    TEST_CHECK( false );
                    return;
                }
            }
}

} // namespace

int main()
{
    TestPackMatchesFigures();
    TestUnpackMatchesFigures();
    TestGeometry();
    TestAlignment();
    TestRoundTripAllBytes();
    TEST_MAIN_RETURN();
}
