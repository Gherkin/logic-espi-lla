#ifndef ESPI_TABLE_OPCODES_H
#define ESPI_TABLE_OPCODES_H

// ---------------------------------------------------------------------------
//  COMMAND OPCODE ENCODINGS
//
//  SOURCE   eSPI Interface Base Specification
//           Table 2: "Command Opcode Encodings", pp.25-27
//           Note 1 (p.27) for the C1C0 request-length encoding
//
//  TRANSCRIPTION HAZARD, recorded because it nearly landed in the code:
//  extracted text renders the short-cycle encodings as "010000C 1 C 0 1",
//  which reads as nine bits. The trailing "1" is a FOOTNOTE MARKER, not a
//  bit -- visible only on the rendered page, where it is superscript. The
//  encodings are eight bits with C1C0 in the low two positions.
//
//  The same extraction also attributes this table to "Figure 13: Command
//  Opcode". On the page it is Table 2. Section metadata from the chunk store
//  is not reliable either.
//
//  MASK column: 0xFF means the encoding is exact. 0xFC means the low two bits
//  are C1C0 and carry the request length rather than identifying the opcode.
// ---------------------------------------------------------------------------

// X( NAME, ENCODING, MASK, CHANNEL, HAS_C1C0 )
#define ESPI_OPCODE_TABLE( X )                                                                                                     \
    /* --- eSPI Peripheral Channel, p.25-26 --- */                                                                                 \
    X( PUT_PC, 0x00, 0xFF, Peripheral, false )                                                                                     \
    X( GET_PC, 0x01, 0xFF, Peripheral, false )                                                                                     \
    X( PUT_NP, 0x02, 0xFF, Peripheral, false )                                                                                     \
    X( GET_NP, 0x03, 0xFF, Peripheral, false )                                                                                     \
    X( PUT_IORD_SHORT, 0x40, 0xFC, Peripheral, true )                                                                              \
    X( PUT_IOWR_SHORT, 0x44, 0xFC, Peripheral, true )                                                                              \
    X( PUT_MEMRD32_SHORT, 0x48, 0xFC, Peripheral, true )                                                                           \
    X( PUT_MEMWR32_SHORT, 0x4C, 0xFC, Peripheral, true )                                                                           \
    /* --- Virtual Wire Channel, p.26 --- */                                                                                       \
    X( PUT_VWIRE, 0x04, 0xFF, VirtualWire, false )                                                                                 \
    X( GET_VWIRE, 0x05, 0xFF, VirtualWire, false )                                                                                 \
    /* --- OOB Message Channel, p.26 --- */                                                                                        \
    X( PUT_OOB, 0x06, 0xFF, Oob, false )                                                                                           \
    X( GET_OOB, 0x07, 0xFF, Oob, false )                                                                                           \
    /* --- Flash Access Channel, p.26-27 --- */                                                                                    \
    X( PUT_FLASH_C, 0x08, 0xFF, Flash, false )                                                                                     \
    X( GET_FLASH_NP, 0x09, 0xFF, Flash, false )                                                                                    \
    X( PUT_FLASH_NP, 0x0A, 0xFF, Flash, false )                                                                                    \
    X( GET_FLASH_C, 0x0B, 0xFF, Flash, false )                                                                                     \
    /* --- Channel Independent, p.27. Enabled by default out of eSPI Reset#. --- */                                                \
    X( GET_STATUS, 0x25, 0xFF, ChannelIndependent, false )                                                                         \
    X( SET_CONFIGURATION, 0x22, 0xFF, ChannelIndependent, false )                                                                  \
    X( GET_CONFIGURATION, 0x21, 0xFF, ChannelIndependent, false )                                                                  \
    X( RESET, 0xFF, 0xFF, ChannelIndependent, false )

// ---------------------------------------------------------------------------
//  C1C0 REQUEST LENGTH -- base spec p.27, Note 1.
//
//  "The opcode encoding C1C0 indicates the length of the request. The address
//   together with the length must not cross the DWord boundary."
//
//  Encoding 10b is Reserved, so a packet carrying it is malformed rather than
//  merely unusual -- length 0 here means "reserved", and the decoder raises it
//  as an error instead of guessing a size.
// ---------------------------------------------------------------------------

// X( ENCODING, LENGTH_BYTES )
#define ESPI_SHORT_LENGTH_TABLE( X )                                                                                               \
    X( 0x0, 1 )                                                                                                                    \
    X( 0x1, 2 )                                                                                                                    \
    X( 0x2, 0 ) /* Reserved */                                                                                                     \
    X( 0x3, 4 )

#endif // ESPI_TABLE_OPCODES_H
