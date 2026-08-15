#ifndef ESPI_IO_MODE_H
#define ESPI_IO_MODE_H

#include <cstdint>

namespace espi
{

// ---------------------------------------------------------------------------
//  I/O mode and lane assignment.
//
//  SOURCE   eSPI Interface Base Specification, section 5.1, Figures 56/57/58
//           ("Single I/O Mode" p.88, "Dual I/O Mode" p.88, "Quad I/O Mode" p.89)
//
//  Read off the rendered waveform diagrams, not the extracted text -- the text
//  layer states only "shifted from bit[7] to bit[0]" and says nothing about
//  which lane carries which bit, nor about the direction split below.
//
//  Two facts, neither of which appears in prose anywhere in the spec:
//
//  1. LANE ORDER. Within one clock the active lanes carry a contiguous group
//     of bits, most significant bit on the HIGHEST numbered lane. Groups are
//     emitted most-significant first.
//
//         Quad,   clock 1:  IO[3]=b7  IO[2]=b6  IO[1]=b5  IO[0]=b4
//                 clock 2:  IO[3]=b3  IO[2]=b2  IO[1]=b1  IO[0]=b0
//         Dual,   clock 1:  IO[1]=b7  IO[0]=b6
//                 clock 2:  IO[1]=b5  IO[0]=b4
//         Single, clock 1:  IO[0]=b7
//                 clock 2:  IO[0]=b6
//
//  2. DIRECTION SPLIT IN SINGLE MODE. Single I/O is not one shared wire. The
//     command phase drives I/O[0] and the response phase drives I/O[1], like
//     MOSI/MISO on plain SPI -- Figure 56 shows I/O[1] flat through the whole
//     command phase and I/O[0] flat through the whole response phase. Dual and
//     Quad instead share their lanes half-duplex, turning around at TAR.
//
//     An analyzer that assumes "single mode means watch I/O[0]" decodes every
//     command correctly and never sees a single response.
// ---------------------------------------------------------------------------

enum class IoMode : uint8_t
{
    Single,
    Dual,
    Quad,
};

enum class Phase : uint8_t
{
    Command,  // controller -> target
    Response, // target -> controller
};

// Bits transferred per serial clock.
constexpr int BitsPerClock( IoMode mode )
{
    return mode == IoMode::Single ? 1 : ( mode == IoMode::Dual ? 2 : 4 );
}

// Serial clocks needed to transfer one byte.
constexpr int ClocksPerByte( IoMode mode )
{
    return 8 / BitsPerClock( mode );
}

// The contiguous run of physical lanes carrying data, as [first, first+count).
struct LaneSet
{
    uint8_t first;
    uint8_t count;

    constexpr bool operator==( const LaneSet& o ) const { return first == o.first && count == o.count; }
};

constexpr LaneSet LanesFor( IoMode mode, Phase phase )
{
    // Single mode is the only case where the phase changes which lane is used.
    return mode == IoMode::Single
               ? LaneSet{ static_cast<uint8_t>( phase == Phase::Command ? 0 : 1 ), 1 }
               : LaneSet{ 0, static_cast<uint8_t>( BitsPerClock( mode ) ) };
}

// Turn-Around window between command and response phases, in serial clocks.
//
// SOURCE  Base Spec section 3.10: "After the 2 clocks Turn-Around (TAR)
//         window, the eSPI target is allowed to respond..."
// Note this is 2 CLOCKS, not 2 byte-times -- so it is a quarter of a byte in
// Single mode and a whole byte in Quad.
constexpr int kTurnAroundClocks = 2;

} // namespace espi

#endif // ESPI_IO_MODE_H
