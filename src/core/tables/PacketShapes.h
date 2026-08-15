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
//  calls for one. All three appends resolve now that the peripheral and flash
//  completion layouts are transcribed -- the modifier also decides which
//  channel's cycle type table the appended header is read against, because
//  GET_STATUS itself is channel independent.
//
//  WHICH PHASE CARRIES THE PACKET is read off the transaction diagrams in
//  section 3, not guessed from the opcode's name. Four figures decide the
//  peripheral rows below, and they do not all agree with the obvious reading:
//
//    --- section 3.8, Non-Posted Transaction ---
//    Figure 24, p.38   PUT_NP HDR CRC      -> ACCEPT HDR DATA STS CRC
//    Figure 25, p.39   PUT_NP HDR CRC      -> DEFER STS CRC
//                      GET_PC CRC          -> ACCEPT HDR DATA STS CRC
//    Figure 26, p.40   PUT_IORD_SHORT HDR CRC       -> RESPONSE Data STS CRC
//                      PUT_IOWR_SHORT HDR Data CRC  -> RESPONSE STS CRC
//                      PUT_MEMRD32_SHORT HDR CRC    -> RESPONSE Data STS CRC
//    Figure 27, p.41   GET_NP CRC          -> ACCEPT HDR STS CRC
//                      PUT_PC HDR DATA CRC -> ACCEPT STS CRC
//
//    --- section 3.9, Posted Transaction ---
//    Figure 28, p.42   PUT_PC HDR DATA CRC -> ACCEPT STS CRC
//    Figure 29, p.42   PUT_MEMWR32_SHORT HDR Data CRC -> RESPONSE STS CRC
//    Figure 30, p.43   GET_PC CRC          -> ACCEPT HDR DATA STS CRC
//
//  THE POSTED FORMS ARE IN A DIFFERENT SECTION, which is easy to miss because
//  Figure 27 already shows a PUT_PC and looks like the last word on it. It is
//  not: PUT_PC is a posted transaction and its own section is 3.9. Figure 28
//  agrees with Figure 27, and Figure 29 draws PUT_MEMWR32_SHORT -- the one
//  short cycle Figure 26 leaves out, because it is the one that is posted.
//
//  Section 3.9 also states a rule no figure carries: "DEFER response for
//  posted transaction is invalid" (p.42). The decoder raises that, because a
//  deferred posted write decodes perfectly and is still a protocol violation.
//
//  Two things fall out of that which a row on its own would not tell you.
//
//  First, a PUT is not always the side carrying the packet. PUT_NP puts a
//  request and gets a *completion* back in the same transaction when the
//  target can answer immediately, so its response phase carries a header and
//  data even though the command did too. The elements marked as completion
//  bearing appear only when the response is ACCEPT -- Figure 25 shows the same
//  command answered with DEFER and no header at all.
//
//  Second, the short reads do not return a completion header. Figure 26 draws
//  their response as `RESPONSE Data STS CRC` -- raw bytes, no cycle type, no
//  tag, no length -- while Figure 24 draws the long form's response with a
//  full header. The two are eight lines apart in the same section and are the
//  obvious pair to flatten into one shape.
//
//  --- OOB AND FLASH: WHY THERE IS NO FIGURE, AND WHAT WAS USED INSTEAD ---
//
//  Section 3 draws no transaction for PUT_OOB, GET_OOB or any flash opcode.
//  Every figure in 3.8 and 3.9 uses a peripheral opcode. So the six rows below
//  rest on two things that are stated rather than drawn.
//
//  The general shape is stated by Figures 62 and 63, p.117, in the error
//  handling section -- the only figures in the document that draw a *generic*
//  transaction. Both label the command phase `CMD | HDR (Optional) | DATA
//  (Optional) | CRC` and the response phase `<error> | STS | CRC`. That is the
//  present-only-on-ACCEPT rule stated for every opcode rather than inferred
//  from the two peripheral figures that happen to show it. The same page says
//  it in words as well, which is better than a figure: "The Response with
//  Non-Fatal Error comprises a Response, a Status and a CRC. There is neither
//  Header nor Data field during the Response phase."
//
//  Which phase carries the packet comes from Table 2's own descriptions,
//  pp.26-27, which are unusually explicit for these six:
//
//    PUT_OOB       "Put an OOB (Tunneled SMBus) message."
//    GET_OOB       "Get an OOB (Tunneled SMBus) message."
//    PUT_FLASH_C   "Put a Flash Access completion. Used in Controller Attached
//                   Flash Sharing mode for the controller to return a flash
//                   access completion to the target."
//    GET_FLASH_NP  "Get a non-posted Flash Access request. Used in Controller
//                   Attached Flash Sharing mode for the target to issue a flash
//                   access request to the controller."
//    PUT_FLASH_NP  "Put a non-posted Flash Access request. Used in Target
//                   Attached Flash Sharing mode for the controller to issue a
//                   flash access request to the target."
//    GET_FLASH_C   "Get a Flash Access completion. Used in Target Attached
//                   Flash Sharing mode for the target to return a flash access
//                   completion to the controller."
//
//  NO FLASH OPCODE CARRIES A COMPLETION IN ITS RESPONSE PHASE, which is the
//  one place these rows differ from the peripheral ones and the one worth
//  being sure about. PUT_NP does carry a completion on ACCEPT -- Figure 24 is
//  captioned "Connected Controller Initiated Non-Posted Transaction" and shows
//  `PUT_NP HDR CRC -> ACCEPT HDR DATA STS CRC`. Copying that to PUT_FLASH_NP
//  would be the obvious move and it is wrong.
//
//  The evidence is that the flash channel has *dedicated completion opcodes*
//  and *dedicated status bits to gate them*, which would be pointless if the
//  completion came back in the request's own response phase. Table 2, p.27:
//  "It is illegal to issue a GET_FLASH_C unless the target has indicated that
//  it has a Flash Access completion available to send" -- that indication is
//  FLASH_C_AVAIL, status bit 12, which section 3.4.2 p.32 says is "only
//  applicable when target attached flash sharing is supported and in
//  operation". The completion is fetched in a later transaction. Section
//  4.2.4.2.1, p.82, describes the queue that makes that necessary: the target
//  "is required to maintain a separate queue for flash access commands" and
//  keeps FLASH_NP_FREE true "unless its corresponding queue is full".
//
//  So the four flash opcodes are two request/completion pairs, one per sharing
//  scheme, and the schemes are mutually exclusive on an interface (4.2.4.2,
//  p.79):
//
//    Controller Attached   target has a request  -> GET_FLASH_NP  (FLASH_NP_AVAIL)
//                          controller answers    -> PUT_FLASH_C   (FLASH_C_FREE)
//    Target Attached       controller requests   -> PUT_FLASH_NP  (FLASH_NP_FREE)
//                          target has an answer  -> GET_FLASH_C   (FLASH_C_AVAIL)
//
//  PUT_OOB is posted -- Table 5 gives the OOB cycle type Command Type
//  "Posted" -- so its response is the posted form of Figure 28: ACCEPT, status,
//  CRC, nothing else.
//
//  ONE CROSS-REFERENCE IN TABLE 2 IS STALE, noted so the next reader does not
//  chase it. PUT_VWIRE and GET_VWIRE say "Refer to Figure 40 for the packet
//  format", but Figure 40 on p.56 is the LTR Message Format and the virtual
//  wire packet is Figure 41 on p.58. PUT_MEMWR32_SHORT says "Refer to Figure
//  37", which is the short *read* format; the short write is Figure 35. Every
//  figure number in this repository is the number printed on the page the
//  figure is on, not the number some other table points at.
// ---------------------------------------------------------------------------

// X( OPCODE_NAME, COMMAND_ELEMENTS, RESPONSE_ELEMENTS )
#define ESPI_PACKET_SHAPE_TABLE( X )                                                                                               \
    /* --- Channel Independent, Figures 20/22/23, pp.35-37 --- */                                                                  \
    X( GET_CONFIGURATION, ESPI_CMD( Addr16 ), ESPI_RSP( Data32, Status16 ) )                                                       \
    X( SET_CONFIGURATION, ESPI_CMD( Addr16, Data32 ), ESPI_RSP( Status16 ) )                                                       \
    X( GET_STATUS, ESPI_CMD(), ESPI_RSP( Status16 ) )                                                                              \
    /* --- Virtual Wire Channel, Figure 41, p.58 --- */                                                                            \
    X( GET_VWIRE, ESPI_CMD(), ESPI_RSP( VwirePacket, Status16 ) )                                                                  \
    X( PUT_VWIRE, ESPI_CMD( VwirePacket ), ESPI_RSP( Status16 ) )                                                                  \
    /* --- Peripheral Channel, Figures 24/25/27, pp.38-41 --- */                                                                   \
    X( PUT_PC, ESPI_CMD( CycleHeader, Payload ), ESPI_RSP( Status16 ) )                                                            \
    X( PUT_NP, ESPI_CMD( CycleHeader, Payload ), ESPI_RSP( CycleHeader, Payload, Status16 ) )                                      \
    X( GET_PC, ESPI_CMD(), ESPI_RSP( CycleHeader, Payload, Status16 ) )                                                            \
    X( GET_NP, ESPI_CMD(), ESPI_RSP( CycleHeader, Payload, Status16 ) )                                                            \
    /* --- Peripheral Channel short cycles, Figure 26, p.40 --- */                                                                 \
    X( PUT_IORD_SHORT, ESPI_CMD( IoAddr16 ), ESPI_RSP( ShortData, Status16 ) )                                                     \
    X( PUT_IOWR_SHORT, ESPI_CMD( IoAddr16, ShortData ), ESPI_RSP( Status16 ) )                                                     \
    X( PUT_MEMRD32_SHORT, ESPI_CMD( MemAddr32 ), ESPI_RSP( ShortData, Status16 ) )                                                 \
    X( PUT_MEMWR32_SHORT, ESPI_CMD( MemAddr32, ShortData ), ESPI_RSP( Status16 ) )                                                 \
    /* --- OOB Message Channel, Table 2 p.26, Figures 62-63 p.117 --- */                                                           \
    X( PUT_OOB, ESPI_CMD( CycleHeader, Payload ), ESPI_RSP( Status16 ) )                                                           \
    X( GET_OOB, ESPI_CMD(), ESPI_RSP( CycleHeader, Payload, Status16 ) )                                                           \
    /* --- Flash Access Channel, Controller Attached, Table 2 pp.26-27 --- */                                                      \
    X( PUT_FLASH_C, ESPI_CMD( CycleHeader, Payload ), ESPI_RSP( Status16 ) )                                                       \
    X( GET_FLASH_NP, ESPI_CMD(), ESPI_RSP( CycleHeader, Payload, Status16 ) )                                                      \
    /* --- Flash Access Channel, Target Attached, Table 2 p.27 --- */                                                              \
    X( PUT_FLASH_NP, ESPI_CMD( CycleHeader, Payload ), ESPI_RSP( Status16 ) )                                                      \
    X( GET_FLASH_C, ESPI_CMD(), ESPI_RSP( CycleHeader, Payload, Status16 ) )

#endif // ESPI_TABLE_PACKET_SHAPES_H
