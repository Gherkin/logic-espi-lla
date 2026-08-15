#ifndef ESPI_STATUS_H
#define ESPI_STATUS_H

#include <cstddef>
#include <cstdint>

namespace espi
{

// One bit of the target status register that trailers every response phase.
//
// Transcribed in src/core/tables/Status.h from base spec Table 4, pp.31-33 and
// Figure 16, p.31 -- that header is where the values live and what the QC
// worksheet checks.
struct StatusBitInfo
{
    const char* name;
    uint8_t bit;
};

// Iteration over the defined bits, in bit order. Reserved positions are not
// listed -- see StatusReservedMask().
size_t StatusBitCount();
const StatusBitInfo& StatusBitAt( size_t index );

// Bits 11:10 and 15:14, which the spec requires the target to drive to '0'.
// A set reserved bit is reported rather than ignored.
uint16_t StatusReservedMask();

} // namespace espi

#endif // ESPI_STATUS_H
