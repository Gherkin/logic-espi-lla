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
//           Section 8.3.2: "In-band RESET Command", pp.122-123
//           Figure 65: "In-band RESET Command", p.123
//
//  THE FRAMING IS A COLUMN, NOT AN INVARIANT. Eighteen of the nineteen rows
//  are `Framed`: the command phase begins with the opcode and ends with a CRC,
//  the response phase begins with the response byte and ends with a CRC, and
//  only the middle varies, so only the middle is tabulated. RESET is the
//  exception and section 8.3.2 is where it says so.
//
//  This used to be written down as an invariant, asserted once in
//  tests/test_link.cpp and left off every row. It was wrong for the whole time
//  RESET sat in the opcode table with no shape beside it.
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
//
//  --- RESET: THE IN-BAND RESET COMMAND, SECTION 8.3.2 pp.122-123 -----------
//
//  Table 2, p.27, gives RESET one line -- "In-band RESET command" -- under
//  Channel Independent, and points nowhere. Section 8.3.2 on p.122 defines the
//  whole transaction and Figure 65 on p.123 draws it, and nothing between the
//  two cross-references either. That is why this row was the last one missing,
//  and it is the failure this repository keeps warning about: a fact no test
//  and no mutation can see, because nobody had read the page.
//
//  Section 8.3.2 p.122, in its own words:
//
//    "RESET command opcode is FFh (i.e. all 1's)."
//    "It is sent with the 20MHz speed or lower."
//    "No CRC byte and thus CRC checking must be ignored."
//    "The transaction has no response phase from eSPI target."
//    "All I/O lines are driven to high ('1') for 16 eSPI clocks and tri-stated
//     at the deassertion edge of CS#, meeting the tSHQZ Output Disable timing."
//
//  and what the target does with it:
//
//    "Ignore all the subsequent bits received."
//    "Bypass or ignore the CRC checking."
//    "Wait until CS# deassertion and assert the in-band reset internally at
//     the CS# deassertion edge." (p.123)
//
//  WHY THE ROW HAS NO ELEMENTS AND NO LENGTH. Two of those sentences pull in
//  opposite directions and both are load bearing. Sixteen clocks is what the
//  *controller* drives, and Figure 65 draws exactly that -- CS# falling,
//  sixteen numbered clocks with all four data lines high, CS# rising. Ignoring
//  every subsequent bit and waiting for the deassertion edge is what the
//  *target* does, and that only means anything if the frame may be longer.
//
//  An analyzer sits where the target sits, so the decoder reads to the chip
//  select edge and treats the sixteen clocks as a property to check rather than
//  a length to read. The capture this repository is pinned against settles it:
//  its opening frame is twelve bytes, ninety-six clocks, and only the first
//  four of them FFh.
//
//  SIXTEEN CLOCKS IS CLOCKS, NOT BYTES, and that is the point of the number.
//  The opcode occupies eight clocks in Single I/O, four in Dual and two in
//  Quad, so sixteen clocks of all ones carries FFh first whichever mode the
//  target believes it is in -- which is what section 8.3.2 means by "the target
//  is able to detect the In-band RESET command opcode regardless of the I/O
//  mode". A whole frame is two bytes in Single, four in Dual, eight in Quad.
//
//  WHAT IT RESETS, p.123: "Offset 008h-00Bh: General Capabilities and
//  Configurations", and "All other target registers are not reset by the
//  In-band RESET, and they must retain their values across the In-band RESET."
//  Section 6.2.1.3, p.94, says the same thing from the register's own side:
//  "This register is also reset by the In-band RESET command."
//
//  TWO SECTIONS POINT BACK AT 8.3.2 AND NEITHER IS TABLE 2. The second is
//  section 5.1, p.86, and it answers a question Figure 65 raises and never
//  settles -- why the figure drives all four data lines when Single I/O gives
//  I/O[1] to the target:
//
//    "In Single I/O mode, I/O[1:0] pins are uni-directional. eSPI controller
//     drives the I/O[0] during command phase, and response from target is
//     driven on the I/O[1]. eSPI target is required to tri-state I/O[1] during
//     command phase as I/O[1] can be driven by eSPI controller such as when
//     initiating an In-Band Reset command."
//
//  So the RESET is the reason the target has to tri-state its own response lane
//  through every command phase, not just this one. Nothing here needs to act on
//  it -- LanesFor( Single, Command ) already reads the opcode off I/O[0] and
//  ignores the rest, which is the right answer whether or not the controller is
//  driving them -- but it is the third statement of this transaction in the
//  document and the only one that explains the figure.
//
//  THAT IS A SESSION STATE FACT, not just a register one. Offset 008h holds
//  I/O Mode Select and CRC Checking Enable, so a RESET puts the bus back to
//  Single I/O with CRC checking off -- the state ConfigResetValue() returns.
//  espi::SessionState is what follows that across transactions.
//
//  TWO FACTS HERE ARE DELIBERATELY NOT IN CODE. The 20MHz ceiling and the
//  tSHQZ tri-state are properties of the waveform, and nothing in this tree
//  carries a frequency or a drive strength to check them against. Recorded
//  here so the next reader does not have to find p.122 again to learn they
//  were read.
// ---------------------------------------------------------------------------

// Figure 65, p.123: the clocks the controller drives every I/O line high for.
#define ESPI_RESET_CLOCKS 16u

// p.123: the inclusive offset range an In-band RESET returns to its default.
#define ESPI_RESET_REGISTER_START 0x008u
#define ESPI_RESET_REGISTER_END 0x00Bu

// X( OPCODE_NAME, COMMAND_ELEMENTS, RESPONSE_ELEMENTS, FRAMING )
#define ESPI_PACKET_SHAPE_TABLE( X )                                                                                               \
    /* --- Channel Independent, Figures 20/22/23, pp.35-37 --- */                                                                  \
    X( GET_CONFIGURATION, ESPI_CMD( Addr16 ), ESPI_RSP( Data32, Status16 ), Framed )                                               \
    X( SET_CONFIGURATION, ESPI_CMD( Addr16, Data32 ), ESPI_RSP( Status16 ), Framed )                                               \
    X( GET_STATUS, ESPI_CMD(), ESPI_RSP( Status16 ), Framed )                                                                      \
    /* --- Channel Independent, section 8.3.2 pp.122-123, Figure 65 p.123 --- */                                                   \
    X( RESET, ESPI_CMD(), ESPI_RSP(), NoCrcNoResponse )                                                                            \
    /* --- Virtual Wire Channel, Figure 41, p.58 --- */                                                                            \
    X( GET_VWIRE, ESPI_CMD(), ESPI_RSP( VwirePacket, Status16 ), Framed )                                                          \
    X( PUT_VWIRE, ESPI_CMD( VwirePacket ), ESPI_RSP( Status16 ), Framed )                                                          \
    /* --- Peripheral Channel, Figures 24/25/27, pp.38-41 --- */                                                                   \
    X( PUT_PC, ESPI_CMD( CycleHeader, Payload ), ESPI_RSP( Status16 ), Framed )                                                    \
    X( PUT_NP, ESPI_CMD( CycleHeader, Payload ), ESPI_RSP( CycleHeader, Payload, Status16 ), Framed )                              \
    X( GET_PC, ESPI_CMD(), ESPI_RSP( CycleHeader, Payload, Status16 ), Framed )                                                    \
    X( GET_NP, ESPI_CMD(), ESPI_RSP( CycleHeader, Payload, Status16 ), Framed )                                                    \
    /* --- Peripheral Channel short cycles, Figure 26, p.40 --- */                                                                 \
    X( PUT_IORD_SHORT, ESPI_CMD( IoAddr16 ), ESPI_RSP( ShortData, Status16 ), Framed )                                             \
    X( PUT_IOWR_SHORT, ESPI_CMD( IoAddr16, ShortData ), ESPI_RSP( Status16 ), Framed )                                             \
    X( PUT_MEMRD32_SHORT, ESPI_CMD( MemAddr32 ), ESPI_RSP( ShortData, Status16 ), Framed )                                         \
    X( PUT_MEMWR32_SHORT, ESPI_CMD( MemAddr32, ShortData ), ESPI_RSP( Status16 ), Framed )                                         \
    /* --- OOB Message Channel, Table 2 p.26, Figures 62-63 p.117 --- */                                                           \
    X( PUT_OOB, ESPI_CMD( CycleHeader, Payload ), ESPI_RSP( Status16 ), Framed )                                                   \
    X( GET_OOB, ESPI_CMD(), ESPI_RSP( CycleHeader, Payload, Status16 ), Framed )                                                   \
    /* --- Flash Access Channel, Controller Attached, Table 2 pp.26-27 --- */                                                      \
    X( PUT_FLASH_C, ESPI_CMD( CycleHeader, Payload ), ESPI_RSP( Status16 ), Framed )                                               \
    X( GET_FLASH_NP, ESPI_CMD(), ESPI_RSP( CycleHeader, Payload, Status16 ), Framed )                                              \
    /* --- Flash Access Channel, Target Attached, Table 2 p.27 --- */                                                              \
    X( PUT_FLASH_NP, ESPI_CMD( CycleHeader, Payload ), ESPI_RSP( Status16 ), Framed )                                              \
    X( GET_FLASH_C, ESPI_CMD(), ESPI_RSP( CycleHeader, Payload, Status16 ), Framed )

#endif // ESPI_TABLE_PACKET_SHAPES_H
