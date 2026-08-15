#ifndef ESPI_TABLE_CYCLE_TYPES_H
#define ESPI_TABLE_CYCLE_TYPES_H

// ---------------------------------------------------------------------------
//  CYCLE TYPES AND PACKET HEADER LAYOUTS
//
//  SOURCE   eSPI Interface Base Specification
//           Table 5: "Cycle Types", pp.47-49 -- the encodings
//           Notes 1-6 to Table 5, p.49 -- P1P0, r2r1r0 and R1R0
//           Section 4.1.1 "Cycle Types", p.46 -- what the columns mean
//           Section 4.1.2 "Tag", p.50
//           Section 4.1.3 "Length", pp.50-51
//           Section 4.1.4 "Address", p.51
//           Figure 34: "Peripheral Memory Write Packet Format", p.53
//           Figure 35: "Short Peripheral Memory or Short I/O Write Packet
//                       Format (Controller Initiated only)", p.53
//           Figure 36: "Peripheral Memory Read Packet Format", p.54
//           Figure 37: "Short Peripheral Memory or Short I/O Read Packet
//                       Format (Controller Initiated only)", p.54
//           Figure 38: "Peripheral Message Packet Format", p.54
//           Figure 39: "Peripheral Memory or I/O Completion With and Without
//                       Data Packet Format", p.55
//           Figure 40: "LTR Message Format", p.56
//           Table 6: "Message Codes", p.55
//           Table 7: "LTR Message Field Description", p.57
//           Figures 24-27, pp.38-41 -- which phase carries which packet
//           Section 4.2.3 "OOB (Tunneled SMBus) Message Channel", pp.72-75
//           Figure 45: "OOB (Tunneled SMBus) Message Packet Format", p.73
//           Figure 46: "OOB MCTP Packet", p.74
//           Figure 47: "OOB Generic SMBus Block Write Format", p.74
//           Section 4.2.4 "Run-time Flash Access Channel", pp.75-83
//           Figure 48: "Flash Access Request Packet Format", p.75
//           Figure 49: "Flash Access Completion Packet Format", p.75
//           Figure 50: "Flash Access RPMC Packet Format", p.76
//           Section 4.2.4.1 "Controller Attached Flash Sharing", pp.77-79
//           Section 4.2.4.2 "Target Attached Flash Sharing", pp.79-83
//           Table 16: "eSPI Flash Access Channel Packet Format for Controller
//                      Attached and Target Attached Flash Configurations", p.81
//           Notes 1-3 to Table 16, p.82
//
//  A CYCLE TYPE BYTE MEANS NOTHING ON ITS OWN. Note 3 on p.49 is explicit:
//  "The combination of command opcode and cycle type encoding must be unique.
//  There is no requirement that cycle type encodings must be unique across
//  command opcodes." And the table exercises that freedom -- 00000000 is
//  Memory Read 32 on the peripheral channel and Flash Read on the flash
//  channel; 00000001 is Memory Write 32 and Flash Write. So every lookup below
//  is keyed by channel, which comes from the opcode, and a table keyed by the
//  byte alone would decode half the bus wrongly while looking perfectly sane.
//
//  TRANSCRIPTION HAZARD -- this is the worst table in the document for it.
//  Three of the encodings carry a subscripted variable field sitting directly
//  against a superscript footnote marker, and extracted text renders both as
//  ordinary digits:
//
//      page       00001P(1)P(0) [1] 1        extraction   00001P 1 P 0 1 1
//      page       00001P(1)P(0) [1,2] 0      extraction   00001P 1 P 0 1,2 0
//      page       0R(1)R(0) [6] 00011        extraction   0R 1 R 0 6 00011
//
//  Read off the rendered page, the encodings are eight bits: the trailing
//  digit is a bit and the raised one is a footnote number. This is the third
//  time this exact hazard has produced a wrong value in this repository -- the
//  nine-bit _SHORT opcode and ACCEPT's R1R0 were the first two.
//
//  "UP" AND "DOWN" ARE DEFINED NOWHERE IN THE TABLE. Section 4.1.1, p.46:
//  "'Up' refers to the direction from eSPI target to eSPI controller and
//  'Down' refers to the direction from eSPI controller to eSPI target."
//
//  BIT 0 IS NOT ARBITRARY. Same section, same page: "The Least-Significant-Bit
//  (LSB) of the encodings distinguishes between a cycle with data and a cycle
//  without data." Every row below agrees with that, which is a useful check on
//  the transcription: Memory Read 32 is even, Memory Write 32 is odd, Message
//  is even, Message with Data is odd, Successful Completion With Data is odd
//  and both without-data completions are even.
//
//  ONE ROW IS "UP" ONLY. The peripheral Successful Completion Without Data is
//  Up, while the flash channel's row of the same name is Up/Down. The two rows
//  sit eight lines apart on two different pages and are otherwise identical;
//  flattening them into one direction is the obvious mistake here.
//
//  TABLE 5 IS NOT THE ONLY TABLE OF FLASH CYCLE TYPES. Table 16 on p.81 lists
//  the same encodings again with an Address Size and a Length column, and it is
//  the only place either is stated for the RPMC rows. The two agree on every
//  encoding, including the R1R0 field position -- which is a genuinely
//  independent second reading of the row that carries the worst extraction
//  hazard in the document.
//
//  They do not agree on everything. Table 16's last two rows describe
//  "05h, 07h, 09h-2Fh" and "30h-3Fh" as Reserved, and 09h-0Fh is where Table 5
//  puts the flash completions -- 00001P1P0 1 and 00001P1P0 0 cover 08h-0Fh.
//  Table 16 conspicuously omits 06h and 08h from its reserved list, which only
//  makes sense if its rows are flash *commands* and completions are out of its
//  scope; but then 09h-0Fh should have been omitted too. Table 5 is treated as
//  the authority on cycle types here and the disagreement is recorded rather
//  than resolved, because resolving it would mean choosing which of two printed
//  tables to disbelieve.
// ---------------------------------------------------------------------------

// X( NAME, ENCODING, MASK, CHANNEL, DIRECTION, COMMAND_TYPE, LAYOUT, VARIABLE )
//
// ENCODING is the byte with every variable field zeroed; MASK has a 0 bit
// wherever the encoding has a variable one, so a match is
// ( byte & MASK ) == ENCODING. The three variable fields, all from p.49:
//
//   P1P0     bits [2:1] of the completion encodings   -- note 1, note 2
//   r2r1r0   bits [3:1] of the message encodings      -- note 5
//   R1R0     bits [6:5] of the RPMC encodings         -- note 6
//
// VARIABLE names which one a row carries, so the decoder resolves the field
// the row actually has rather than guessing from the bit pattern.
//
// LAYOUT names the packet format figure the row points at. NotTranscribed is
// a row whose figure nobody has read yet -- the decoder reports the cycle type
// and stops rather than inventing a header length. No row carries it now that
// the OOB and flash figures have been read; the value is kept because it is
// the mechanism that keeps a future gap a gap instead of a guess.
#define ESPI_CYCLE_TYPE_TABLE( X )                                                                                                 \
    /* --- eSPI Peripheral Channel, Table 5 pp.47-48 --- */                                                                        \
    X( "Memory Read 32", 0x00, 0xFF, Peripheral, UpOrDown, NonPosted, MemoryRead32, None )                                         \
    X( "Memory Write 32", 0x01, 0xFF, Peripheral, UpOrDown, Posted, MemoryWrite32, None )                                          \
    X( "Memory Read 64", 0x02, 0xFF, Peripheral, UpOrDown, NonPosted, MemoryRead64, None )                                         \
    X( "Memory Write 64", 0x03, 0xFF, Peripheral, UpOrDown, Posted, MemoryWrite64, None )                                          \
    X( "Successful Completion Without Data", 0x06, 0xFF, Peripheral, Up, Completion, CompletionWithoutData, None )                 \
    X( "Unsuccessful Completion Without Data", 0x08, 0xF9, Peripheral, UpOrDown, Completion, CompletionWithoutData,                \
       SplitCompletion )                                                                                                           \
    X( "Successful Completion With Data", 0x09, 0xF9, Peripheral, UpOrDown, Completion, CompletionWithData, SplitCompletion )      \
    X( "Message", 0x10, 0xF1, Peripheral, UpOrDown, Posted, Message, MessageRouting )                                              \
    X( "Message with Data", 0x11, 0xF1, Peripheral, UpOrDown, Posted, MessageWithData, MessageRouting )                            \
    /* --- OOB Message Channel, Table 5 p.48. Figure 45, p.73. --- */                                                              \
    X( "OOB (Tunneled SMBus) Message", 0x21, 0xFF, Oob, UpOrDown, Posted, OobMessage, None )                                       \
    /* --- Flash Access Channel, Table 5 pp.48-49. Figures 48-50, pp.75-76. --- */                                                 \
    X( "Flash Read", 0x00, 0xFF, Flash, UpOrDown, NonPosted, FlashRead, None )                                                     \
    X( "Flash Write", 0x01, 0xFF, Flash, UpOrDown, NonPosted, FlashWrite, None )                                                   \
    X( "Flash Erase", 0x02, 0xFF, Flash, UpOrDown, NonPosted, FlashErase, None )                                                   \
    X( "RPMC Op.1", 0x03, 0x9F, Flash, Down, NonPosted, FlashRpmcOp1, RpmcTarget )                                                 \
    X( "RPMC Op.2", 0x04, 0x9F, Flash, Down, NonPosted, FlashRpmcOp2, RpmcTarget )                                                 \
    X( "Successful Completion Without Data", 0x06, 0xFF, Flash, UpOrDown, Completion, FlashCompletionWithoutData, None )           \
    X( "Unsuccessful Completion Without Data", 0x08, 0xF9, Flash, UpOrDown, Completion, FlashCompletionWithoutData,                \
       SplitCompletion )                                                                                                           \
    X( "Successful Completion With Data", 0x09, 0xF9, Flash, UpOrDown, Completion, FlashCompletionWithData,                        \
       SplitCompletion )

// ---------------------------------------------------------------------------
//  NOTE 1, p.49 -- the split completion field P1P0.
//
//  Section 4.1.3 on p.51 says what it is for: "For successful completion with
//  data and unsuccessful completion without data, the additional cycle type
//  encoding indicates whether the completion is the first, middle or the last
//  completion for a split completion sequence, or whether it is the only
//  completion that completes the split transaction."
//
//  The order is not the order a reader expects -- 00b is the *middle*, not the
//  first -- so it is transcribed in the page's order rather than sorted.
//
//  NOTE 2, p.49, is a constraint rather than an encoding: "For Unsuccessful
//  Completion without Data, P1 must be always a '1' as this is always the last
//  or the only completion." So P1P0 of 00b or 01b on an unsuccessful
//  completion is malformed, and the decoder says so instead of rendering a
//  middle completion that cannot exist.
// ---------------------------------------------------------------------------

// X( ENCODING, TEXT )
#define ESPI_CYCLE_SPLIT_COMPLETION_TABLE( X )                                                                                     \
    X( 0x0, "middle completion of a split completion sequence" )                                                                   \
    X( 0x1, "first completion of a split completion sequence" )                                                                    \
    X( 0x2, "last completion of a split completion sequence" )                                                                     \
    X( 0x3, "only completion for a split transaction" )

#define ESPI_CYCLE_SPLIT_COMPLETION_SHIFT 1u
#define ESPI_CYCLE_SPLIT_COMPLETION_MASK 0x3u
// Note 2: P1 -- the high bit of the field -- must be set on an unsuccessful
// completion without data.
#define ESPI_CYCLE_SPLIT_P1_MASK 0x2u

// ---------------------------------------------------------------------------
//  NOTE 5, p.49 -- the message routing field r2r1r0.
//
//  Only one encoding is defined. 001b-111b is a single Reserved row on the
//  page, and it is transcribed as a range rather than expanded, because seven
//  identical rows invite a reader to skim them.
// ---------------------------------------------------------------------------

// X( ENCODING, TEXT )
#define ESPI_CYCLE_MESSAGE_ROUTING_TABLE( X ) X( 0x0, "Local -- terminated at receiver" ) /* 001b-111b Reserved */

#define ESPI_CYCLE_MESSAGE_ROUTING_SHIFT 1u
#define ESPI_CYCLE_MESSAGE_ROUTING_MASK 0x7u

// ---------------------------------------------------------------------------
//  NOTE 6, p.49 -- the RPMC target field R1R0.
//
//  The page prints the note as "The encoding R1R0 has the following
//  definition" and then heads the table's own column "P1P0". That is a typo in
//  the specification: the field in the RPMC encodings is R1R0, note 1 already
//  defines P1P0 as something else, and the four rows describe flash devices
//  rather than split completions. Recorded here so the next reader does not
//  have to work it out twice.
// ---------------------------------------------------------------------------

// X( ENCODING, TEXT )
#define ESPI_CYCLE_RPMC_TARGET_TABLE( X )                                                                                          \
    X( 0x0, "1st RPMC flash device" )                                                                                              \
    X( 0x1, "2nd RPMC flash device" )                                                                                              \
    X( 0x2, "3rd RPMC flash device" )                                                                                              \
    X( 0x3, "4th RPMC flash device" )

#define ESPI_CYCLE_RPMC_TARGET_SHIFT 5u
#define ESPI_CYCLE_RPMC_TARGET_MASK 0x3u

// ---------------------------------------------------------------------------
//  PACKET HEADER LAYOUTS -- Figures 34, 36, 38 and 39, pp.53-55 (peripheral),
//  Figure 45, p.73 (OOB), and Figures 48, 49 and 50, pp.75-76 (flash).
//
//  X( LAYOUT, FIGURE, HEADER_BYTES, ADDRESS_BYTES, HAS_MESSAGE_CODE,
//     HAS_PAYLOAD, LENGTH, PAYLOAD )
//
//  Every one of these headers opens the same way (Figure 33, p.46):
//
//      Byte 0   Cycle Type
//      Byte 1   Tag [7:4] | Length[11:8] [3:0]
//      Byte 2   Length[7:0]
//
//  and then differs. HEADER_BYTES counts from the cycle type byte, so it is
//  always 3 + ADDRESS_BYTES + (HAS_MESSAGE_CODE ? 5 : 0). It is transcribed
//  rather than computed because it is what the figure's brace actually spans,
//  and a test checks the two against each other -- the same trick the virtual
//  wire table uses for the valid/level pairing.
//
//  HAS_PAYLOAD is whether data bytes follow the header, which is the LSB rule
//  from section 4.1.1 seen from the other side. Note that a Memory Read
//  request has a meaningful Length and no payload: the length is the size
//  being *asked for*, and the bytes come back in a completion later. Flash Read
//  and RPMC Op.2 are the flash channel's two instances of the same thing.
//
//  LENGTH is where section 4.1.3, pp.50-51, earns its place -- Table 5 and the
//  figures between them say nothing about how to read the field:
//
//    OneBased    "The length field is 1-based. A value of all zeros indicates
//                4 KB of length." So 000h is 4096 bytes, not zero. A decoder
//                that prints "0 bytes" here is wrong in the single most common
//                case, and nothing in the packet contradicts it. Table 16
//                note 3, p.82, restates it for the flash channel.
//    MustBeZero  "For Completion without Data or Un-Successful Completion, the
//                length field must be driven to zeros by initiator. The
//                receiver must ignore the length field." The 4 KB reading does
//                not apply, and a nonzero value is the initiator misbehaving.
//    Reserved    "For Message cycle type, the Length field is Reserved and it
//                must be sent with all 0s" (p.55). Message carries no payload,
//                so there is nothing for a length to count.
//    BlockErase  Flash Erase only. Not a byte count at all -- see the erase
//                size table below. Section 4.1.3 does not state this; it
//                forwards to 4.2.4.1 and 4.2.4.2.
//
//                Following the cross-reference the table itself gives you does
//                not help, which is what makes this sharp. Table 5's Flash
//                Erase row ends "Refer to Figure 48 for the packet format",
//                exactly as Flash Read and Flash Write do. Figure 48 then
//                draws Flash Erase sharing the read's header, Length field and
//                all, and says nothing whatever about erase sizes. So a reader
//                who follows Table 5 to its own named figure sees a Length
//                field with no hint that it is not a byte count, and never
//                reaches Table 16.
//
//  PAYLOAD says whether the data bytes after the header have a transcribed
//  structure. It is a column rather than something the decoder infers from the
//  layout name, for the same reason the message code table has a FIELDS column:
//  matching on a name would make a rename silently change what the decoder
//  does.
//
//    Opaque       data bytes, no structure this document gives
//    SmbusPacket  Figure 45, p.73 -- the tunneled SMBus block write
//    RpmcOpcode   Figure 50, p.76 -- data byte 0 is the RPMC OP1 opcode
//
//  ONE THING FIGURE 50 NAMES THAT IS NOT DECODABLE HERE. It draws data byte 0
//  of an RPMC OP2 *completion* as "Extended Status". That completion carries
//  the ordinary Successful Completion With Data cycle type, shared with every
//  other flash read completion, so nothing in the packet says the request it
//  answers was an RPMC OP2. Naming the byte would take matching the Tag against
//  an earlier transaction, which is L2 state this layer does not have. Recorded
//  rather than guessed.
//
//  FLASH COMPLETIONS GET THEIR OWN ROWS even though Figure 49 draws exactly
//  what Figure 39 draws, and section 4.2.4 says so in prose: "The Flash Access
//  channel uses the same packet format as the eSPI Peripheral Channel
//  transactions" (p.75). Two rows rather than one shared row because a row here
//  is meant to be checkable against one figure cell in seconds, and because
//  Table 5 note 2's P1 rule then has to be stated as applying to both.
// ---------------------------------------------------------------------------
#define ESPI_CYCLE_HEADER_LAYOUT_TABLE( X )                                                                                        \
    /* --- eSPI Peripheral Channel --- */                                                                                          \
    X( MemoryRead32, "Figure 36, p.54", 7, 4, false, false, OneBased, Opaque )                                                     \
    X( MemoryRead64, "Figure 36, p.54", 11, 8, false, false, OneBased, Opaque )                                                    \
    X( MemoryWrite32, "Figure 34, p.53", 7, 4, false, true, OneBased, Opaque )                                                     \
    X( MemoryWrite64, "Figure 34, p.53", 11, 8, false, true, OneBased, Opaque )                                                    \
    X( Message, "Figure 38, p.54", 8, 0, true, false, Reserved, Opaque )                                                           \
    X( MessageWithData, "Figure 38, p.54", 8, 0, true, true, OneBased, Opaque )                                                    \
    X( CompletionWithData, "Figure 39, p.55", 3, 0, false, true, OneBased, Opaque )                                                \
    X( CompletionWithoutData, "Figure 39, p.55", 3, 0, false, false, MustBeZero, Opaque )                                          \
    /* --- OOB Message Channel --- */                                                                                              \
    X( OobMessage, "Figure 45, p.73", 3, 0, false, true, OneBased, SmbusPacket )                                                   \
    /* --- Flash Access Channel --- */                                                                                             \
    X( FlashRead, "Figure 48, p.75", 7, 4, false, false, OneBased, Opaque )                                                        \
    X( FlashWrite, "Figure 48, p.75", 7, 4, false, true, OneBased, Opaque )                                                        \
    X( FlashErase, "Figure 48, p.75", 7, 4, false, false, BlockErase, Opaque )                                                     \
    X( FlashRpmcOp1, "Figure 50, p.76", 3, 0, false, true, OneBased, RpmcOpcode )                                                  \
    X( FlashRpmcOp2, "Figure 50, p.76", 3, 0, false, false, OneBased, Opaque )                                                     \
    X( FlashCompletionWithData, "Figure 49, p.75", 3, 0, false, true, OneBased, Opaque )                                           \
    X( FlashCompletionWithoutData, "Figure 49, p.75", 3, 0, false, false, MustBeZero, Opaque )

// The Tag and Length split of byte 1, and the width of the whole Length field.
// Section 4.1.2, p.50: "The 4-bit Tag field allows up to 16 unique non-posted
// requests to be outstanding at any one time."
#define ESPI_CYCLE_TAG_SHIFT 4u
#define ESPI_CYCLE_TAG_MASK 0xFu
#define ESPI_CYCLE_LENGTH_HIGH_MASK 0xFu
#define ESPI_CYCLE_LENGTH_BITS 12u
// Section 4.1.3, p.50: "A value of all zeros indicates 4 KB of length."
#define ESPI_CYCLE_LENGTH_ZERO_MEANS 4096u

// ---------------------------------------------------------------------------
//  SHORT CYCLE HEADERS -- Figures 35 and 37, pp.53-54, and section 3.8, p.39.
//
//  The short cycles are the one peripheral packet with no cycle type byte at
//  all. Section 3.8, p.39: "The unique opcode indicates the type of non-posted
//  transaction and the request length. The header contains the address only
//  and the number of address bytes for the transaction is implied by the
//  opcode. The short non-posted transaction does not have the Tag field. The
//  Tag field is implied as all 0's which will be returned by the target in the
//  completion header."
//
//  So there is no cycle type, no tag and no length field on the wire -- the
//  length is C1C0 in the opcode (Table 2 note 1, p.27) and section 4.1.3
//  restates it: "For Short I/O and Short Memory, there is no length field
//  defined as the length of the transaction is embedded in the command opcode
//  itself which supports 1, 2, or 4 bytes access. Short command does not
//  support 3 bytes access."
//
//  Address widths are read off the figures: 2 bytes for I/O, 4 for memory,
//  most significant byte first in both.
// ---------------------------------------------------------------------------
#define ESPI_CYCLE_SHORT_IO_ADDRESS_BYTES 2u
#define ESPI_CYCLE_SHORT_MEMORY_ADDRESS_BYTES 4u

// ---------------------------------------------------------------------------
//  TABLE 6: MESSAGE CODES, p.55.
//
//  X( CODE, NAME, CYCLE_TYPE, ROUTING, DIRECTION, FIELDS, DESCRIPTION )
//
//  The whole table is one row. "The Message Code in the packet header defines
//  the functionality and usage of the message" (p.55), and LTR is the only
//  code the base specification defines, so any other code is a message this
//  document does not name rather than a malformed one.
//
//  FIELDS says whether the four message specific bytes of Figure 38 have a
//  transcribed layout behind them. It is a column rather than something the
//  decoder works out from the name, because matching on the text "LTR" would
//  make renaming a row silently change what the decoder does -- and because a
//  message code with no field layout and a message code this table has never
//  heard of are different findings.
// ---------------------------------------------------------------------------
#define ESPI_MESSAGE_CODE_TABLE( X ) X( 0x01, "LTR", "Message", 0x0, Up, true, "Latency Tolerance Reporting" )

// ---------------------------------------------------------------------------
//  FIGURE 40, p.56, AND TABLE 7, p.57 -- the LTR message.
//
//  Figure 40 lays the four message specific bytes out as
//
//      Byte 4   RQ [7] | RSV [6:5] | Latency Scale [2:0] at [4:2] | LV[9:8] at [1:0]
//      Byte 5   Latency Value [7:0]
//      Byte 6   Reserved
//      Byte 7   Reserved
//
//  so the Latency Value is ten bits split across two bytes. "LTR uses Message
//  cycle Type with no data payload" (p.56), which is why Figure 40's Length
//  cells are drawn as literal 0h and 00h rather than as fields.
//
//  Table 7 gives RQ its meaning: "A '0' indicates that eSPI target has no
//  service requirement. When this bit is a '1', the remaining fields are valid
//  to indicate latency tolerance requirement for the eSPI target." A decoder
//  that prints a latency under a clear RQ is reporting a number the target
//  explicitly said not to read -- the same shape of mistake as reading a
//  virtual wire level under a clear valid bit.
// ---------------------------------------------------------------------------
#define ESPI_LTR_RQ_BIT 7u
#define ESPI_LTR_RSV_SHIFT 5u
#define ESPI_LTR_RSV_MASK 0x3u
#define ESPI_LTR_SCALE_SHIFT 2u
#define ESPI_LTR_SCALE_MASK 0x7u
#define ESPI_LTR_VALUE_HIGH_MASK 0x3u
#define ESPI_LTR_VALUE_BITS 10u

// X( ENCODING, NANOSECONDS, TEXT )
//
// Table 7, p.57: "This is the multiplier to the Latency Value (LV[9:0]) field
// to yield an absolute time value for the latency tolerance." Encodings 110b
// and 111b are one Reserved row on the page and are absent here, so they
// report as an undefined scale rather than as a multiplier of zero.
#define ESPI_LTR_SCALE_TABLE( X )                                                                                                  \
    X( 0x0, 1u, "1 ns" )                                                                                                           \
    X( 0x1, 32u, "32 ns" )                                                                                                         \
    X( 0x2, 1024u, "1,024 ns" )                                                                                                    \
    X( 0x3, 32768u, "32,768 ns" )                                                                                                  \
    X( 0x4, 1048576u, "1,048,576 ns" )                                                                                             \
    X( 0x5, 33554432u, "33,554,432 ns" )

// ---------------------------------------------------------------------------
//  FLASH ERASE BLOCK SIZES -- Table 16, p.81, the Length column of the 02h row.
//
//  X( ENCODING, TARGET_ATTACHED, CONTROLLER_ATTACHED )
//
//  THE SAME LENGTH FIELD MEANS TWO DIFFERENT THINGS, and which one is not on
//  the bus. Table 16 prints two separate lists under one cell, one for each
//  flash sharing scheme, and they disagree on every encoding except 2h:
//
//      encoding   Target Attached      Controller Attached
//      0h         4 KB                 Reserved
//      1h         32 KB                4 KB
//      2h         64 KB                64 KB
//      3h         128 KB               Reserved
//      4h         Reserved             128 KB
//      5h         Reserved             256 KB
//      6h-FFFh    Reserved             Reserved
//      (4h-FFFh Reserved on the Target Attached side)
//
//  A length of 1h is 32 KB on one scheme and 4 KB on the other. Nothing in the
//  packet says which scheme is in operation -- it is the Flash Sharing Mode bit
//  of configuration register 040h (bit 11, p.102), set before the channel is
//  enabled, and the two schemes are "mutually exclusive for a given eSPI
//  interface" (4.2.4.2, p.79). So the honest decode names both readings, the
//  same answer the General-Purpose I/O Expander direction gets: the
//  specification is not silent, it says the value varies.
//
//  An empty string is Table 16 printing "Reserved" in that column, which is a
//  statement rather than a gap. Encodings above 5h appear in neither column and
//  are Reserved in both.
//
//  NOTHING IN TABLE 5 SENDS A READER HERE. Its Flash Erase row ends "Refer to
//  Figure 48 for the packet format" and that is the only cross-reference it
//  offers -- the same sentence Flash Read and Flash Write carry, pointing at a
//  figure that draws the Length field and defines nothing about it. Table 16 is
//  reachable only from section 4.2.4.2.1 or by reading section 4.1.3's
//  forwarding sentence and following it. A transcription that stopped at
//  Table 5 and its named figure would be complete by its own lights.
//
//  (That sentence is also missing from extracted text: the chunk store's copy
//  of the Flash Erase cell ends at "corresponding flash controller" and drops
//  the figure reference, while keeping it on the Flash Read and Flash Write
//  rows. A wrapped table cell losing its last line is a new shape of the
//  extraction hazard -- the earlier ones mangled values, this one deletes a
//  sentence and leaves a plausible cell behind.)
//
//  SECTION 4.2.4.1 SAYS IT SLIGHTLY DIFFERENTLY, p.78: "the least significant
//  3 bits of the length field specifies the size of the block to be erased. The
//  encoding of the least significant 3 bits of the length field matches the
//  value of the Flash Block Erase Size field of the Channel Capabilities and
//  Configuration register." Table 16 gives the whole 12-bit field instead and
//  reserves everything above the defined values, which is the stricter reading
//  and is what is transcribed. The same page adds "length field encoding of
//  '011' is not applicable for Flash Erase in Controller Attached Flash
//  Sharing", which is Table 16's 3h: Reserved cell said twice.
//
//  That 011b is worth a second look rather than a shrug. Register 040h bits
//  4:2 give 011b as "Both 4 Kbytes and 64 Kbytes are supported" -- a statement
//  about what the device can do, not a size a single erase request could name.
//  So the two are consistent: a capability of "both" is not a request length.
// ---------------------------------------------------------------------------
#define ESPI_FLASH_ERASE_SIZE_TABLE( X )                                                                                           \
    X( 0x0, "4 KB", "" )                                                                                                           \
    X( 0x1, "32 KB", "4 KB" )                                                                                                      \
    X( 0x2, "64 KB", "64 KB" )                                                                                                     \
    X( 0x3, "128 KB", "" )                                                                                                         \
    X( 0x4, "", "128 KB" )                                                                                                         \
    X( 0x5, "", "256 KB" )

// ---------------------------------------------------------------------------
//  OOB TUNNELED SMBUS PACKET -- Figure 45, p.73, and section 4.2.3, pp.72-75.
//
//  The OOB header is the ordinary three bytes and every SMBus field is *data*.
//  Figure 45's brace is explicit about it: Header spans bytes 0-2, Data spans
//  byte 3 to the end. Section 4.2.3 p.73 says the same in prose -- "The SMBus
//  Target Address, SMBus Command Opcode, SMBus Byte Count, SMBus Data fields
//  and the optional PEC byte are sent as data within the eSPI OOB message
//  packet."
//
//      Byte 3   SMBus Target Address [7:1] | 0 [0]
//      Byte 4   SMBus Command Opcode
//      Byte 5   SMBus Byte Count
//      Byte 6   SMBus Data Byte 0
//      ...
//      Byte n+6 SMBus Data Byte n
//      Byte n+7 PEC          <- drawn with a dashed border: optional
//
//  BIT 0 OF BYTE 3 IS DRAWN AS A LITERAL '0', in its own narrow cell against
//  the 7..0 bit ruler, not as a field. That is the SMBus read/write bit on a
//  block write. Figure 46 draws the same cell as a literal '1' on the MCTP
//  Source Target Address byte, which is what makes it clear the cell is a bit
//  and not part of the address.
//
//  WHETHER THERE IS A PEC BYTE IS ARITHMETIC, NOT A FLAG. Section 4.2.3, p.73:
//  "The presence of SMBus PEC is determined through a simple arithmetic
//  operation between the eSPI OOB header length field and the SMBus Byte
//  Count." The two sentences that make it computable are on the same page:
//
//    "The SMBus Byte Count field does not include the PEC byte. It comprehends
//     the actual payload of the SMBus block write packet itself excluding the
//     3 SMBus header bytes."
//    "The Length field of the OOB message comprehends the count by the SMBus
//     Byte Count field, in addition to the 3 header bytes (i.e. SMBus Target
//     Address, SMBus Command Opcode and SMBus Byte Count) and an optional PEC
//     byte."
//
//  so Length = 3 + ByteCount + (PEC ? 1 : 0), and PEC = Length - 3 - ByteCount
//  which must come out 0 or 1. Figures 46 and 47 work the arithmetic twice on
//  the page: (3+5+64+1) = 73 with ByteCount 69 for MCTP, and (3+64+1) = 68 with
//  ByteCount 64 for a generic block write.
//
//  Any other difference is a malformed packet -- and it is one that decodes
//  perfectly otherwise, because the OOB Length alone says how many bytes to
//  read. Nothing else on the bus notices.
// ---------------------------------------------------------------------------

// Bytes 3, 4 and 5 -- the SMBus header, counted as part of the OOB Length.
#define ESPI_OOB_SMBUS_HEADER_BYTES 3u
#define ESPI_OOB_SMBUS_ADDRESS_SHIFT 1u
#define ESPI_OOB_SMBUS_ADDRESS_MASK 0x7Fu
// Figure 45: byte 3 bit 0 is drawn as the literal '0'.
#define ESPI_OOB_SMBUS_ADDRESS_BIT0_MASK 0x01u
#define ESPI_OOB_SMBUS_ADDRESS_BIT0_EXPECTED 0u

// ---------------------------------------------------------------------------
//  TABLE OF SMBUS COMMAND OPCODES THIS DOCUMENT NAMES -- section 4.2.3, p.73,
//  and Figure 46, p.74.
//
//  X( CODE, NAME, HEADER_BYTES )
//
//  One row, for the same reason Table 6 has one row: the base specification
//  names exactly one. "MCTP over SMBus is a specific form of the SMBus block
//  write packet with the SMBus Command Opcode of 0Fh (i.e. MCTP)" (p.73), and
//  Figure 46 draws it with the cell reading "Command Code = MCTP = 0Fh".
//
//  HEADER_BYTES is how many of the SMBus data bytes the embedded protocol
//  spends on its own header before its payload starts. Figure 46's right hand
//  brace labels it "MCTP Header (5 bytes)" and its worked example is
//  ByteCount = (5+64) = 69, so the count includes the header and the MPS does
//  not: "the Maximum Payload Size (MPS) applies to the MCTP payload itself
//  excluding the MCTP header and the optional PEC byte" (p.73).
// ---------------------------------------------------------------------------
#define ESPI_OOB_SMBUS_COMMAND_TABLE( X ) X( 0x0F, "MCTP", 5 )

// ---------------------------------------------------------------------------
//  MCTP HEADER -- Figure 46, p.74, the five bytes at packet offsets 6 to 10.
//
//  X( BYTE, HIGH, LOW, NAME )
//
//  BYTE is the offset within the MCTP header, so 0 here is packet byte 6.
//
//  EXTRACTION HAZARD OF A NEW KIND, and the reason this block is called out
//  separately in the QC sheet. Every other bit range in this repository was
//  read from a printed bit number -- "Length[11:8]", "Address[31:24]", a Table
//  4 row saying "bit 3". Figure 46's byte 10 prints no bit numbers at all: it
//  draws five cells side by side, labelled SOM, EOM, Packet Seq#, TO and
//  Message Tag, and the only statement of their widths is where the cell
//  borders fall against the 7|6|5|4|3|2|1|0 ruler at the top of the column.
//  The ranges below are read off those borders. That is weaker evidence than
//  anything else here and it is marked as such rather than blended in.
//
//  Byte 7's split is the same kind of reading but a much easier one: two cells
//  of visibly equal width, so [7:4] and [3:0].
//
//  THE FIELD NAMES ARE eSPI'S; THE MEANINGS ARE NOT. Figure 46 labels these
//  cells and says nothing about what any value means -- MCTP is defined by
//  DSP0237, which is not in this database. So the decoder prints the name and
//  the number and stops. It does not, for instance, claim to know what a
//  Message Tag of 2 signifies, because this document never says.
// ---------------------------------------------------------------------------

#define ESPI_OOB_MCTP_SOURCE_BIT0_MASK 0x01u
// Figure 46: the Source Target Address byte's bit 0 is drawn as the literal '1'.
#define ESPI_OOB_MCTP_SOURCE_BIT0_EXPECTED 1u

#define ESPI_OOB_MCTP_HEADER_TABLE( X )                                                                                            \
    X( 0, 7, 1, "Source Target Address" )                                                                                          \
    X( 1, 7, 4, "MCTP Reserved" )                                                                                                  \
    X( 1, 3, 0, "Header Version" )                                                                                                 \
    X( 2, 7, 0, "Destination Endpoint ID" )                                                                                        \
    X( 3, 7, 0, "Source Endpoint ID" )                                                                                             \
    X( 4, 7, 7, "SOM" )                                                                                                            \
    X( 4, 6, 6, "EOM" )                                                                                                            \
    X( 4, 5, 4, "Packet Seq#" )                                                                                                    \
    X( 4, 3, 3, "TO" )                                                                                                             \
    X( 4, 2, 0, "Message Tag" )

#endif // ESPI_TABLE_CYCLE_TYPES_H
