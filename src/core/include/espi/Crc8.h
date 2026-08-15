#ifndef ESPI_CRC8_H
#define ESPI_CRC8_H

#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace espi
{

// ---------------------------------------------------------------------------
//  eSPI CRC-8
//
//  SOURCE   eSPI Interface Base Specification, section 5.2 "Cyclic Redundancy
//           Check (CRC)", p.90
//
//  Spec text, verbatim:
//    - polynomial x^8 + x^2 + x + 1, coefficient 07h
//    - seed value 00h; registers reset to 00h before any CRC calculation
//    - calculation starts with bit[7] of byte 0, proceeds bit[7] -> bit[0]
//
//  No reflection, no final XOR. Equivalent to CRC-8/SMBUS with a 00h seed.
//
//  What each phase's CRC covers -- the part most worth checking at QC-1,
//  because a second implementation of the algorithm would not catch an error
//  here:
//
//    Command phase   opcode + header (if present) + data (if present)
//    Response phase  response code + header (if present) + data (if present)
//                    + status, EXCLUDING any WAIT_STATE response bytes
//
//  Verified 10/10 against the third-party capture in tests/vectors/, which
//  independently confirms both the parameters above and the response-phase
//  span including the status trailer. See tests/test_crc.cpp.
// ---------------------------------------------------------------------------

class Crc8
{
  public:
    static constexpr uint8_t kPolynomial = 0x07;
    static constexpr uint8_t kSeed = 0x00;

    void Reset() { mCrc = kSeed; }

    void Update( uint8_t byte );
    void Update( const uint8_t* data, size_t length );

    uint8_t Value() const { return mCrc; }

    static uint8_t Compute( const uint8_t* data, size_t length );
    static uint8_t Compute( std::initializer_list<uint8_t> bytes );

  private:
    uint8_t mCrc = kSeed;
};

} // namespace espi

#endif // ESPI_CRC8_H
