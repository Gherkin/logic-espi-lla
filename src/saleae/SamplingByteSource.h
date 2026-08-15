#ifndef ESPI_SAMPLING_BYTE_SOURCE_H
#define ESPI_SAMPLING_BYTE_SOURCE_H

#include "espi/ByteStream.h"
#include "espi/IoMode.h"
#include "espi/LaneCodec.h"

#include <cstdint>

class AnalyzerChannelData;

namespace espi_saleae
{

// ---------------------------------------------------------------------------
//  L0 -- sampling.
//
//  The only layer in the tree that knows what a clock edge is, and the only
//  one that includes a Saleae header on the decode path. It turns CS#, CLK and
//  I/O[3:0] into bytes with sample spans, and hands them to the pure core
//  through espi::ByteSource.
//
//  THE SAMPLING RULE, and where it comes from.
//
//  SOURCE  eSPI Base Specification, section 3, p.21 (prose), with Figure 10
//          ("Basic eSPI Protocol", section 3.1, p.21) rendered to confirm it:
//
//      "The Serial Clock must be low at the assertion edge of the Chip Select#
//       while eSPI Reset# has been de-asserted. The first data is launched from
//       controller while the serial clock is still low and sampled on the first
//       rising edge of the clock by target. Subsequent data is launched on the
//       falling edge of the clock from controller and sampled on the rising
//       edge of the clock by target. The data is launched from target on the
//       falling edge of the clock."
//
//  The controller launches on the falling edge and the target launches on the
//  falling edge. Both are therefore sampled on the rising edge, and the whole
//  of L0 reduces to one rule that does not care which phase it is in:
//
//      Sample every active lane on every rising edge of CLK while CS# is
//      asserted.
//
//  TAR is two of those rising edges, consumed and never interpreted. Section
//  3.3, p.28 is explicit that the controller drives the lines high for the
//  first Turn-Around clock and tri-states thereafter, with the weak pull-up
//  holding them high -- so there is nothing in that window to read, and a
//  decoder that tried to read it would find pull-up state and call it data.
//
//  WHAT THIS CLASS DELIBERATELY DOES NOT DO. It never looks at a byte it has
//  produced. It cannot tell a command from a response except by being told,
//  cannot compute a packet length, and cannot reach src/core/tables/. That is
//  the pull interface's whole purpose -- see the note in espi/ByteStream.h for
//  why L0 splitting the phases itself is impossible outside Single I/O.
// ---------------------------------------------------------------------------

class SamplingByteSource : public espi::ByteSource
{
  public:
    // Lanes not used by the configured I/O mode may be null. Single and Dual
    // mode leave io[2] and io[3] unconnected, which is a legitimate capture
    // setup, not an error.
    struct Channels
    {
        AnalyzerChannelData* cs = nullptr;
        AnalyzerChannelData* clk = nullptr;
        AnalyzerChannelData* io[ 4 ] = { nullptr, nullptr, nullptr, nullptr };
    };

    SamplingByteSource( const Channels& channels, espi::IoMode mode );

    // Advance to the next CS# assertion and arm the source for one
    // transaction.
    //
    // If the capture happens to start with CS# already asserted we are in the
    // middle of somebody else's transaction, with no way to know how far in.
    // This skips to the end of it and starts at the next assertion rather than
    // decoding a fragment -- resynchronising at a CS# edge is the contract
    // LinkDecoder is written against.
    //
    // Propagates the SDK's end-of-data condition rather than swallowing it:
    // offline that is an exception the harness treats as normal worker
    // termination, and in Logic 2 it blocks until more data arrives. Either
    // way it is not L0's decision to make.
    bool SyncToNextAssertion();

    // Sample numbers of the CS# edges bounding the armed transaction.
    uint64_t AssertSample() const { return mAssertSample; }
    uint64_t DeassertSample() const { return mDeassertSample; }

    // Read per transaction rather than fixed at construction: an accepted
    // SET_CONFIGURATION to offset 08h changes the I/O mode at the CS#
    // deassertion edge. Acting on that is phase 7; this is the seam it needs.
    void SetMode( espi::IoMode mode ) { mMode = mode; }

    // --- espi::ByteSource ---
    bool ReadByte( espi::Phase phase, espi::StreamByte* out ) override;
    bool TurnAround( espi::ByteSpan* span ) override;
    bool Active() const override;
    espi::IoMode Mode() const override;

  private:
    // Advance CLK to the next rising edge inside the armed transaction.
    // Returns false and disarms once the next edge would fall at or after the
    // CS# deassertion, which is what stops the decoder reading past the
    // boundary.
    bool NextSamplingEdge( uint64_t* sample );

    // Advance the lanes active for `phase` to `sample` and read their levels.
    bool SampleLanes( espi::Phase phase, uint64_t sample, espi::LaneBits* out );

    Channels mChannels;
    espi::IoMode mMode;
    uint64_t mAssertSample = 0;
    uint64_t mDeassertSample = 0;
    bool mArmed = false;
};

} // namespace espi_saleae

#endif // ESPI_SAMPLING_BYTE_SOURCE_H
