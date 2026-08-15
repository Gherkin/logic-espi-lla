#ifndef ESPI_TABLE_PACKET_SHAPES_H
#define ESPI_TABLE_PACKET_SHAPES_H

// ---------------------------------------------------------------------------
//  PACKET SHAPES
//
//  What sits between the framing of each phase.
//
//  SOURCE   eSPI Interface Base Specification
//           Figure 20: "GET_STATUS Command", p.35
//           Figure 21: "GET_STATUS Command (with Response Modifier)", p.36
//           Figure 22: "GET_CONFIGURATION Command", p.37
//           Figure 23: "SET_CONFIGURATION Command", p.37
//           Figure 41: "Virtual Wire Packet Format", p.58
//           Section 3.7, p.37, states the config shapes in prose as well
//
//  THE FRAMING IS IMPLICIT. Every command phase begins with the opcode and
//  ends with a CRC; every response phase begins with the response byte and
//  ends with a CRC. Only the middle varies, so only the middle is tabulated.
//  That invariant is asserted once in tests/test_link.cpp rather than repeated
//  on every row -- a row here is meant to be checkable against one figure in
//  seconds.
//
//  BYTE ORDER TRAVELS WITH THE ELEMENT, because eSPI mixes it within a single
//  packet and getting it wrong is silent. Section 5.1, p.86:
//
//      Address  MSB first   <- big endian
//      Data     LSB first   <- little endian
//      Status   LSB first   <- little endian
//
//  So a GET_CONFIGURATION of register 0020h puts 00h then 20h on the bus,
//  while the DWord it reads back arrives least significant byte first.
//
//  GET_STATUS AND THE RESPONSE MODIFIER. Figure 21 shows a GET_STATUS response
//  carrying an appended packet ahead of the status trailer, selected by R1R0
//  in the response byte (Table 3 Note 1, p.30). The row below gives the
//  unappended shape; the decoder inserts the appended packet when the modifier
//  calls for one. Only the virtual wire append is transcribed -- a peripheral
//  or flash completion needs the cycle-type header layouts, which are not
//  transcribed yet, and is reported as an explicit gap.
// ---------------------------------------------------------------------------

// X( OPCODE_NAME, COMMAND_ELEMENTS, RESPONSE_ELEMENTS )
#define ESPI_PACKET_SHAPE_TABLE( X )                                                                                               \
    /* --- Channel Independent, Figures 20/22/23, pp.35-37 --- */                                                                  \
    X( GET_CONFIGURATION, ESPI_CMD( Addr16 ), ESPI_RSP( Data32, Status16 ) )                                                       \
    X( SET_CONFIGURATION, ESPI_CMD( Addr16, Data32 ), ESPI_RSP( Status16 ) )                                                       \
    X( GET_STATUS, ESPI_CMD(), ESPI_RSP( Status16 ) )                                                                              \
    /* --- Virtual Wire Channel, Figure 41, p.58 --- */                                                                            \
    X( GET_VWIRE, ESPI_CMD(), ESPI_RSP( VwirePacket, Status16 ) )                                                                  \
    X( PUT_VWIRE, ESPI_CMD( VwirePacket ), ESPI_RSP( Status16 ) )

#endif // ESPI_TABLE_PACKET_SHAPES_H
