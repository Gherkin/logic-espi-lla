#ifndef ESPI_TABLE_STATUS_H
#define ESPI_TABLE_STATUS_H

// ---------------------------------------------------------------------------
//  TARGET STATUS REGISTER
//
//  SOURCE   eSPI Interface Base Specification
//           Table 4: "Status Field Encodings", pp.31-33
//           Figure 16: "Target's Status Register Definition", p.31
//           Section 3.4.2 "Status", p.31
//
//  Sixteen bits, trailing every response phase. Bits 11:10 and 15:14 are
//  Reserved and the spec requires the target to drive them to '0', so a
//  nonzero reserved bit is worth reporting rather than hiding.
//
//  TRANSCRIPTION HAZARD: the chunk store's description of Figure 16 places the
//  four FLASH bits at "bits 15-14 and 13-12". The rendered figure and Table 4
//  both put them at 8, 9, 12 and 13, with the reserved pairs at 11:10 and
//  15:14. The stored description is wrong; the page is right.
//
//  The same extraction never names this table at all -- the section metadata
//  says only "3.4.2 Status". On the page it is Table 4.
//
//  VWIRE_FREE (bit 2) and FLASH_C_FREE (bit 8) are documented as always '1'.
//  Figure 16 marks both with a literal '1' in the register diagram. They are
//  not treated specially here: the decoder reports what is on the bus.
//
//  Byte order on the wire is LSB first -- see section 5.1, p.86. That is the
//  opposite of the address field in the same packet.
// ---------------------------------------------------------------------------

// X( NAME, BIT )
#define ESPI_STATUS_BIT_TABLE( X )                                                                                                 \
    /* --- Target's Rx queues Free, p.31-32 --- */                                                                                 \
    X( PC_FREE, 0 )                                                                                                                \
    X( NP_FREE, 1 )                                                                                                                \
    X( VWIRE_FREE, 2 )                                                                                                             \
    X( OOB_FREE, 3 )                                                                                                               \
    /* --- Target's Tx queues Available, p.32 --- */                                                                               \
    X( PC_AVAIL, 4 )                                                                                                               \
    X( NP_AVAIL, 5 )                                                                                                               \
    X( VWIRE_AVAIL, 6 )                                                                                                            \
    X( OOB_AVAIL, 7 )                                                                                                              \
    /* --- Target's Rx queues Free, flash channel, p.32 --- */                                                                     \
    X( FLASH_C_FREE, 8 )                                                                                                           \
    X( FLASH_NP_FREE, 9 )                                                                                                          \
    /* --- Target's Tx queues Available, flash channel, pp.32-33 --- */                                                            \
    X( FLASH_C_AVAIL, 12 )                                                                                                         \
    X( FLASH_NP_AVAIL, 13 )

// Bits 11:10 and 15:14. Table 4, pp.32-33.
#define ESPI_STATUS_RESERVED_MASK 0xCC00u

#endif // ESPI_TABLE_STATUS_H
