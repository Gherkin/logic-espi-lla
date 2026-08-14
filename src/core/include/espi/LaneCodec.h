#ifndef ESPI_LANE_CODEC_H
#define ESPI_LANE_CODEC_H

#include "espi/IoMode.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace espi
{

// Levels on the four physical I/O lanes for one serial clock, indexed by
// absolute lane number. Lanes outside the active LaneSet are ignored on
// unpack and left false on pack.
using LaneBits = std::array<bool, 4>;

// ---------------------------------------------------------------------------
//  Bit <-> lane conversion. See IoMode.h for the transcribed lane order.
//
//  NOTE ON RULE R3 -- this is the one module deliberately shared between the
//  decoder and the test serializer, and it is the only such module.
//
//  It has to be shared: a waveform test is impossible unless the code laying
//  bits down agrees with the code picking them up. The plan enumerates this as
//  an unavoidable shared fact rather than pretending the seam is total.
//
//  It is pinned two ways, neither of which is a round-trip against itself:
//    - tests/test_lanes.cpp asserts Pack AND Unpack separately against literal
//      lane tables transcribed from Figures 56/57/58. A wrong lane order fails
//      both directions independently, so they cannot agree their way to green.
//    - the golden capture came off real hardware in Single mode, so a wrong
//      convention also fails T4.
//
//  What must NOT happen here: no opcode, cycle-type, virtual-wire or register
//  knowledge. This module converts bits to lanes and nothing else. Protocol
//  meaning lives behind the private tables seam.
// ---------------------------------------------------------------------------

// Bit position (0..7) carried by lane offset `lane_offset` within bit group
// `group`, for a mode transferring `bits_per_clock` bits per clock.
//
//     bit = 7 - group*width - (width-1) + lane_offset
//
// Verified against all three figures: Quad g0 k3 -> b7, Quad g1 k0 -> b0,
// Dual g1 k1 -> b5, Single g1 k0 -> b6.
constexpr int BitPositionFor( int group, int lane_offset, int bits_per_clock )
{
    return 7 - group * bits_per_clock - ( bits_per_clock - 1 ) + lane_offset;
}

// Serialize one byte into ClocksPerByte(mode) clocks of lane levels.
// Writes to out[0 .. ClocksPerByte(mode)-1]; returns the number written.
size_t PackByte( IoMode mode, Phase phase, uint8_t value, LaneBits* out, size_t out_capacity );

// Accumulates lane levels clock by clock and emits completed bytes.
class ByteAssembler
{
  public:
    ByteAssembler( IoMode mode, Phase phase );

    // Feed one clock's lane levels. Returns true and writes *out when a byte
    // completes; returns false while a byte is still in progress.
    bool Feed( const LaneBits& lanes, uint8_t* out );

    // Discard a partially accumulated byte. Called at CS# edges and at TAR,
    // where a byte boundary is guaranteed by the framing rather than by count.
    void Reset();

    // True when no partial byte is pending -- i.e. the stream is byte aligned.
    bool Aligned() const { return mGroup == 0; }

    IoMode Mode() const { return mMode; }
    Phase GetPhase() const { return mPhase; }

  private:
    IoMode mMode;
    Phase mPhase;
    LaneSet mLanes;
    int mGroupsPerByte;
    int mGroup = 0;
    uint8_t mAccum = 0;
};

} // namespace espi

#endif // ESPI_LANE_CODEC_H
