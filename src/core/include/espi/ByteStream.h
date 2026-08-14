#ifndef ESPI_BYTE_STREAM_H
#define ESPI_BYTE_STREAM_H

#include "espi/IoMode.h"

#include <cstdint>

namespace espi
{

// Inclusive sample range a decoded element occupies. The core never
// interprets these -- it carries them through so the Saleae shell can turn a
// field back into a Frame with the right start and end.
struct ByteSpan
{
    uint64_t first = 0;
    uint64_t last = 0;

    bool Valid() const { return last >= first; }
};

inline ByteSpan Merge( const ByteSpan& a, const ByteSpan& b )
{
    return ByteSpan{ a.first < b.first ? a.first : b.first, a.last > b.last ? a.last : b.last };
}

struct StreamByte
{
    uint8_t value = 0;
    ByteSpan span{};
};

// ---------------------------------------------------------------------------
//  Pull interface between L0 (sampling) and L1 (link decoding).
//
//  WHY PULL RATHER THAN PUSH. The obvious design has L0 hand the decoder a
//  pre-split transaction -- command bytes here, response bytes there. That
//  cannot work outside Single I/O mode.
//
//  In Single mode the phases are physically separable: command is on I/O[0],
//  response on I/O[1] (see IoMode.h). But Dual and Quad share their lanes
//  half-duplex, and the only thing marking where the command ends is the
//  packet length -- which depends on the opcode, and on header fields inside
//  the packet. Splitting the phases therefore requires protocol knowledge,
//  which is exactly what L0 must not have.
//
//  So the decoder drives: it reads bytes until it knows the command is
//  complete, calls TurnAround(), then reads the response. L0 stays mechanical
//  and the packet grammar stays in one place.
//
//  Implementations:
//    - the Saleae shell walks AnalyzerChannelData and clock edges
//    - tests feed literal bytes from a fixture (rule R1)
// ---------------------------------------------------------------------------

class ByteSource
{
  public:
    virtual ~ByteSource() = default;

    // Read one byte in the given phase. Returns false when chip select
    // deasserts or the capture runs out mid-byte.
    virtual bool ReadByte( Phase phase, StreamByte* out ) = 0;

    // Consume the turn-around window between command and response phases.
    // Returns false if chip select deasserts inside it. Writes the samples the
    // window occupied when span is non-null.
    virtual bool TurnAround( ByteSpan* span ) = 0;

    // True while chip select is asserted -- i.e. the transaction continues.
    virtual bool Active() const = 0;

    // Current I/O mode. Read per transaction rather than cached, because
    // SET_CONFIGURATION can change it between transactions.
    virtual IoMode Mode() const = 0;
};

} // namespace espi

#endif // ESPI_BYTE_STREAM_H
