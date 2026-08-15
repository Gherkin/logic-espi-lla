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
//  NOT TRANSCRIBED YET, deliberately: the packet layouts behind the OOB
//  (Figure 45) and flash (Figures 48 and 50) cycle types. Those are stage E.
//  The encodings are transcribed here because Table 5 is one table and reading
//  half of it twice is how a table drifts from the page; the layouts report as
//  an explicit gap, which is a different answer from "not defined".
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
// and stops rather than inventing a header length.
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
    /* --- OOB Message Channel, Table 5 p.48. Figure 45 is stage E. --- */                                                         \
    X( "OOB (Tunneled SMBus) Message", 0x21, 0xFF, Oob, UpOrDown, Posted, NotTranscribed, None )                                   \
    /* --- Flash Access Channel, Table 5 pp.48-49. Figures 48 and 50 are stage E. --- */                                           \
    X( "Flash Read", 0x00, 0xFF, Flash, UpOrDown, NonPosted, NotTranscribed, None )                                                \
    X( "Flash Write", 0x01, 0xFF, Flash, UpOrDown, NonPosted, NotTranscribed, None )                                               \
    X( "Flash Erase", 0x02, 0xFF, Flash, UpOrDown, NonPosted, NotTranscribed, None )                                               \
    X( "RPMC Op.1", 0x03, 0x9F, Flash, Down, NonPosted, NotTranscribed, RpmcTarget )                                               \
    X( "RPMC Op.2", 0x04, 0x9F, Flash, Down, NonPosted, NotTranscribed, RpmcTarget )                                               \
    X( "Successful Completion Without Data", 0x06, 0xFF, Flash, UpOrDown, Completion, NotTranscribed, None )                       \
    X( "Unsuccessful Completion Without Data", 0x08, 0xF9, Flash, UpOrDown, Completion, NotTranscribed, SplitCompletion )          \
    X( "Successful Completion With Data", 0x09, 0xF9, Flash, UpOrDown, Completion, NotTranscribed, SplitCompletion )

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
//  PACKET HEADER LAYOUTS -- Figures 34, 36, 38 and 39, pp.53-55.
//
//  X( LAYOUT, FIGURE, HEADER_BYTES, ADDRESS_BYTES, HAS_MESSAGE_CODE,
//     HAS_PAYLOAD, LENGTH )
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
//  being *asked for*, and the bytes come back in a completion later.
//
//  LENGTH is where section 4.1.3, pp.50-51, earns its place -- Table 5 and the
//  figures between them say nothing about how to read the field:
//
//    OneBased    "The length field is 1-based. A value of all zeros indicates
//                4 KB of length." So 000h is 4096 bytes, not zero. A decoder
//                that prints "0 bytes" here is wrong in the single most common
//                case, and nothing in the packet contradicts it.
//    MustBeZero  "For Completion without Data or Un-Successful Completion, the
//                length field must be driven to zeros by initiator. The
//                receiver must ignore the length field." The 4 KB reading does
//                not apply, and a nonzero value is the initiator misbehaving.
//    Reserved    "For Message cycle type, the Length field is Reserved and it
//                must be sent with all 0s" (p.55). Message carries no payload,
//                so there is nothing for a length to count.
// ---------------------------------------------------------------------------
#define ESPI_CYCLE_HEADER_LAYOUT_TABLE( X )                                                                                        \
    X( MemoryRead32, "Figure 36, p.54", 7, 4, false, false, OneBased )                                                             \
    X( MemoryRead64, "Figure 36, p.54", 11, 8, false, false, OneBased )                                                            \
    X( MemoryWrite32, "Figure 34, p.53", 7, 4, false, true, OneBased )                                                             \
    X( MemoryWrite64, "Figure 34, p.53", 11, 8, false, true, OneBased )                                                            \
    X( Message, "Figure 38, p.54", 8, 0, true, false, Reserved )                                                                   \
    X( MessageWithData, "Figure 38, p.54", 8, 0, true, true, OneBased )                                                            \
    X( CompletionWithData, "Figure 39, p.55", 3, 0, false, true, OneBased )                                                        \
    X( CompletionWithoutData, "Figure 39, p.55", 3, 0, false, false, MustBeZero )

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

#endif // ESPI_TABLE_CYCLE_TYPES_H
