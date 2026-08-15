#ifndef ESPI_TABLE_RESPONSES_H
#define ESPI_TABLE_RESPONSES_H

// ---------------------------------------------------------------------------
//  RESPONSE FIELD ENCODINGS
//
//  SOURCE   eSPI Interface Base Specification
//           Table 3: "Response Field Encodings", p.30
//           Note 1 (p.30) for the R1R0 response modifier
//           Figure 15: "Response Field", p.29
//           Section 3.10 "WAIT STATE", pp.44-45
//
//  FIELD LAYOUT of the response byte, read off the rendered page:
//
//      [7:6]  R1R0   response modifier
//      [5:4]  RSV    reserved, driven to 0 by the target
//      [3:0]  response code, the table below
//
//  TRANSCRIPTION HAZARDS on this page, both recorded because they nearly
//  landed in the code:
//
//  1. The chunk store's description of Figure 15 places RSV at *bit [5]* and
//     the response code at [3:0], leaving bit 4 unaccounted for. The rendered
//     page column header says [5:4]. The page wins.
//
//  2. ACCEPT's encoding column reads "R1R0" followed by a superscript footnote
//     marker 1, which text extraction flattens to "R 1 R 0 1" -- the same
//     footnote-as-a-bit hazard that nearly corrupted the opcode table. The
//     modifier is two bits.
//
//  MATCHING ORDER MATTERS. NO_RESPONSE is the exact byte FFh: [7:6]=11,
//  [5:4]=11, [3:0]=1111. WAIT_STATE shares the low nibble 1111 but requires
//  [7:6]=00. Match NO_RESPONSE as a whole byte *before* masking to the low
//  nibble, or every NO_RESPONSE decodes as a wait state and the decoder hangs
//  waiting for a response that the spec says will never be driven.
//
//  R1R0 is meaningful only for GET_STATUS with an ACCEPT response. Note 1
//  states it is "always 00" otherwise -- except NO_RESPONSE, where it is 11.
// ---------------------------------------------------------------------------

// X( NAME, CODE )   -- CODE is bits [3:0] of the response byte
#define ESPI_RESPONSE_CODE_TABLE( X )                                                                                              \
    X( ACCEPT, 0x8 )                                                                                                               \
    X( DEFER, 0x1 )                                                                                                                \
    X( NON_FATAL_ERROR, 0x2 )                                                                                                      \
    X( FATAL_ERROR, 0x3 )                                                                                                          \
    X( WAIT_STATE, 0xF )

// The whole-byte encoding for NO_RESPONSE, matched before the table above.
#define ESPI_RESPONSE_NO_RESPONSE_BYTE 0xFF
#define ESPI_RESPONSE_NO_RESPONSE_NAME "NO_RESPONSE"

// ---------------------------------------------------------------------------
//  RESPONSE MODIFIER -- base spec p.30, Note 1.
//
//  Only defined for GET_STATUS with an ACCEPT response. It says which packet,
//  if any, is appended to the response phase ahead of the status trailer.
// ---------------------------------------------------------------------------

// X( ENCODING, DESCRIPTION )
#define ESPI_RESPONSE_MODIFIER_TABLE( X )                                                                                          \
    X( 0x0, "no append" )                                                                                                          \
    X( 0x1, "Peripheral (channel 0) completion appended" )                                                                         \
    X( 0x2, "Virtual Wire (channel 1) packet appended" )                                                                           \
    X( 0x3, "Flash Access (channel 3) completion appended" )

#endif // ESPI_TABLE_RESPONSES_H
