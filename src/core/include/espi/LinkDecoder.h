#ifndef ESPI_LINK_DECODER_H
#define ESPI_LINK_DECODER_H

#include "espi/ByteStream.h"
#include "espi/Decode.h"

namespace espi
{

// ---------------------------------------------------------------------------
//  L1 -- the link layer.
//
//  Walks one chip-select-delimited transaction: opcode, header, command CRC,
//  turn-around, response byte, any WAIT_STATE run, payload, status, response
//  CRC. It drives the ByteSource rather than being fed by it -- see the note
//  in ByteStream.h for why that direction is forced by Dual and Quad mode.
//
//  ERROR BEHAVIOUR is part of the contract, not an afterthought. Real captures
//  start mid-transaction and contain glitches, so:
//
//    - a malformed packet produces error fields and stops, it never guesses
//    - the decoder never reads past a chip select boundary
//    - the next Decode() starts clean at the next assertion, so one bad
//      transaction cannot desynchronise the ones after it
//
//  An opcode with no transcribed packet shape is reported as an explicit gap.
//  That is deliberate: a decoder that invents a length for an opcode nobody
//  has read off the specification produces confident nonsense.
// ---------------------------------------------------------------------------

class LinkDecoder
{
  public:
    explicit LinkDecoder( ByteSource* source );

    // Decode the next transaction. Returns false when the source has no
    // further transaction to offer.
    bool Decode( Transaction* out );

  private:
    ByteSource* mSource;
};

} // namespace espi

#endif // ESPI_LINK_DECODER_H
