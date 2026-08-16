#ifndef ESPI_TABLE_CONFIG_REGISTERS_H
#define ESPI_TABLE_CONFIG_REGISTERS_H

// ---------------------------------------------------------------------------
//  CAPABILITIES AND CONFIGURATION REGISTERS
//
//  SOURCE   eSPI Interface Base Specification, section 6.2, pp.92-103
//           Table 21: "Target Registers", p.93 (the address map)
//           6.2.1.2 Offset 04h: Device Identification, p.94
//           6.2.1.3 Offset 08h: General Capabilities and Configurations, pp.94-96
//           6.2.1.4 Offset 10h: Channel 0 Capabilities and Configurations, pp.97-98
//           6.2.1.5 Offset 20h: Channel 1 Capabilities and Configurations, p.99
//           6.2.1.6 Offset 30h: Channel 2 Capabilities and Configurations, p.100
//           6.2.1.7 Offset 40h: Channel 3 Capabilities and Configurations, pp.101-103
//           6.2.1.8 Offset 44h: Channel 3 Capabilities and Configurations 2, pp.104-105
//           6.2.1.9 Offset 48h: Channel 3 Capabilities and Configurations 3, p.106
//           6.2.1.10 Offset 4Ch: Channel 3 Capabilities and Configurations 4, p.106
//
//  ADDRESSING -- three separate rules, §3.7 pp.37-38, and they are not the
//  same rule stated three ways:
//
//    1. The target has a 4 KB register space, so only the low 12 bits of the
//       16-bit address select a register.
//    2. "address bit[1:0] hard-wired to always 00" -- access is DWord
//       granular, so a register is only ever addressed at its base.
//    3. "The 4 MSB address bits must be driven to all zeros by eSPI
//       controller. eSPI targets should ignore the 4 MSB address bits."
//
//  Rule 3 is the one worth being careful about. Because the target ignores
//  bits 15:12, an address of F020h still reaches the Channel 1 register -- but
//  it is a controller that is violating the specification, and an analyzer
//  that quietly masks the address off hides exactly the kind of bug an
//  analyzer exists to find. F020h and 0020h are not the same address; one of
//  them is malformed and happens to work. The same goes for a nonzero bit[1:0].
//
//  Both are reported rather than corrected. Neither stops the decode.
//
//  RANGES, NOT OFFSETS. Table 21 gives a Start and an End for every entry --
//  Device Identification is 004h through 007h, not 004h. The whole map is
//  transcribed below, reserved rows included, so it can be checked against the
//  page row for row. Only ranges marked as decoded have a field layout.
//
//  044h, 048h AND 04Ch ARE NEW IN REVISION 1.6. The revision history on p.8
//  lists "Fixed the table of Target Registers to include registers at offset
//  44h, 48h and 4Ch" against March 2025, and the RPMC content itself arrived in
//  1.5 as "ECN- Target Attached Flash RPMC". Every field in all three is RO,
//  and every one that carries information is HwInit -- they are entirely a
//  description of what flash hardware sits behind the target, so none of them
//  contributes to the reset state.
//
//  WHY THE ENABLE AND READY BITS MATTER TO THE DECODER ITSELF. This is not
//  just labelling: a SET_CONFIGURATION to offset 08h that is accepted changes
//  the I/O mode and whether CRC checking is on, and it takes effect at the
//  *deassertion edge of Chip Select#* (§6.2, p.92, and the I/O Mode Select
//  field on p.95). An analyzer that ignores this decodes the first few hundred
//  microseconds of a capture and then produces garbage.
// ---------------------------------------------------------------------------

// X( START, END, NAME, KIND )
//
// Every row of Table 21, p.93, in page order. KIND is ours, not the page's:
//
//   Fields    the range is a register and its bit layout is transcribed below
//   NoFields  Table 21 names it, but nobody has transcribed its bits yet
//   Reserved  Table 21 says Reserved or Platform Specific
//
// The distinction between NoFields and Reserved matters to a reader of the
// decode: "a register we have not done yet" and "not a register" are different
// answers to the same question.
#define ESPI_CONFIG_REGISTER_TABLE( X )                                                                                            \
    X( 0x000, 0x003, "Reserved", Reserved )                                                                                        \
    X( 0x004, 0x007, "Device Identification", Fields )                                                                             \
    X( 0x008, 0x00B, "General Capabilities and Configurations", Fields )                                                           \
    X( 0x00C, 0x00F, "Reserved", Reserved )                                                                                        \
    X( 0x010, 0x013, "Channel 0 Capabilities and Configurations", Fields )                                                         \
    X( 0x014, 0x01F, "Reserved", Reserved )                                                                                        \
    X( 0x020, 0x023, "Channel 1 Capabilities and Configurations", Fields )                                                         \
    X( 0x024, 0x02F, "Reserved", Reserved )                                                                                        \
    X( 0x030, 0x033, "Channel 2 Capabilities and Configurations", Fields )                                                         \
    X( 0x034, 0x03F, "Reserved", Reserved )                                                                                        \
    X( 0x040, 0x043, "Channel 3 Capabilities and Configurations", Fields )                                                         \
    X( 0x044, 0x047, "Channel 3 Capabilities and Configurations 2", Fields )                                                       \
    X( 0x048, 0x04B, "Channel 3 Capabilities and Configurations 3", Fields )                                                       \
    X( 0x04C, 0x04F, "Channel 3 Capabilities and Configurations 4", Fields )                                                       \
    X( 0x050, 0x7FF, "Reserved", Reserved )                                                                                        \
    X( 0x800, 0xFFF, "Platform Specific registers", Reserved )

// ---------------------------------------------------------------------------
//  VALUE ENCODINGS
//
//  Only fields the specification gives an encoding table for appear here.
//  Single-bit enables and status flags carry their meaning in their name and
//  are rendered as the bit value.
// ---------------------------------------------------------------------------

// X( ENUM, VALUE, TEXT )
#define ESPI_CONFIG_ENUM_TABLE( X )                                                                                                \
    /* --- I/O Mode Select, offset 08h bits 27:26, p.95 --- */                                                                     \
    X( IoModeSelect, 0x0, "Single I/O" )                                                                                           \
    X( IoModeSelect, 0x1, "Dual I/O" )                                                                                             \
    X( IoModeSelect, 0x2, "Quad I/O" )                                                                                             \
    X( IoModeSelect, 0x3, "Reserved" )                                                                                             \
    /* --- I/O Mode Support, offset 08h bits 25:24, p.95 --- */                                                                    \
    X( IoModeSupport, 0x0, "Single I/O" )                                                                                          \
    X( IoModeSupport, 0x1, "Single and Dual I/O" )                                                                                 \
    X( IoModeSupport, 0x2, "Single and Quad I/O" )                                                                                 \
    X( IoModeSupport, 0x3, "Single, Dual and Quad I/O" )                                                                           \
    /* --- Frequency, offset 08h bits 22:20 and 18:16, pp.95-96 --- */                                                             \
    X( Frequency, 0x0, "20 MHz" )                                                                                                  \
    X( Frequency, 0x1, "25 MHz" )                                                                                                  \
    X( Frequency, 0x2, "33 MHz" )                                                                                                  \
    X( Frequency, 0x3, "50 MHz" )                                                                                                  \
    X( Frequency, 0x4, "66 MHz" )                                                                                                  \
    X( Frequency, 0x5, "Reserved" )                                                                                                \
    X( Frequency, 0x6, "Reserved" )                                                                                                \
    X( Frequency, 0x7, "Reserved" )                                                                                                \
    /* --- Max payload size, offsets 10h/30h/40h, pp.97,100,102-103 --- */                                                         \
    X( MaxPayload, 0x0, "Reserved" )                                                                                               \
    X( MaxPayload, 0x1, "64 bytes" )                                                                                               \
    X( MaxPayload, 0x2, "128 bytes" )                                                                                              \
    X( MaxPayload, 0x3, "256 bytes" )                                                                                              \
    X( MaxPayload, 0x4, "Reserved" )                                                                                               \
    X( MaxPayload, 0x5, "Reserved" )                                                                                               \
    X( MaxPayload, 0x6, "Reserved" )                                                                                               \
    X( MaxPayload, 0x7, "Reserved" )                                                                                               \
    /* --- Max read request size, offsets 10h/40h, pp.97,102 --- */                                                                \
    X( MaxReadRequest, 0x0, "Reserved" )                                                                                           \
    X( MaxReadRequest, 0x1, "64 bytes" )                                                                                           \
    X( MaxReadRequest, 0x2, "128 bytes" )                                                                                          \
    X( MaxReadRequest, 0x3, "256 bytes" )                                                                                          \
    X( MaxReadRequest, 0x4, "512 bytes" )                                                                                          \
    X( MaxReadRequest, 0x5, "1024 bytes" )                                                                                         \
    X( MaxReadRequest, 0x6, "2048 bytes" )                                                                                         \
    X( MaxReadRequest, 0x7, "4096 bytes" )                                                                                         \
    /* --- Flash Sharing Capability Supported, offset 40h bits 17:16, p.101 --- */                                                 \
    X( FlashSharingCap, 0x0, "target attached: no, controller attached: yes" )                                                     \
    X( FlashSharingCap, 0x1, "target attached: no, controller attached: yes" )                                                     \
    X( FlashSharingCap, 0x2, "target attached: yes, controller attached: no" )                                                     \
    X( FlashSharingCap, 0x3, "target attached: yes, controller attached: yes" )                                                    \
    /* --- Flash Block Erase Size, offset 40h bits 4:2, p.103 --- */                                                               \
    X( FlashBlockErase, 0x0, "Reserved" )                                                                                          \
    X( FlashBlockErase, 0x1, "4 Kbytes" )                                                                                          \
    X( FlashBlockErase, 0x2, "64 Kbytes" )                                                                                         \
    X( FlashBlockErase, 0x3, "4 Kbytes and 64 Kbytes" )                                                                            \
    X( FlashBlockErase, 0x4, "128 Kbytes" )                                                                                        \
    X( FlashBlockErase, 0x5, "256 Kbytes" )                                                                                        \
    X( FlashBlockErase, 0x6, "Reserved" )                                                                                          \
    X( FlashBlockErase, 0x7, "Reserved" )                                                                                          \
    /* --- Flash Sharing Mode, offset 40h bit 11, p.102 --- */                                                                     \
    X( FlashSharingMode, 0x0, "controller attached flash sharing" )                                                                \
    X( FlashSharingMode, 0x1, "target attached flash sharing" )                                                                    \
    /* --- Number of Target Attached Flash RPMC flash devices, 44h bits 23:22, p.104 --- */                                        \
    X( RpmcDeviceCount, 0x0, "1 RPMC flash device" )                                                                               \
    X( RpmcDeviceCount, 0x1, "2 RPMC flash devices" )                                                                              \
    X( RpmcDeviceCount, 0x2, "3 RPMC flash devices" )                                                                              \
    X( RpmcDeviceCount, 0x3, "4 RPMC flash devices" )                                                                              \
    /* --- Target Maximum Read Request Size Supported, 44h bits 2:0, p.105.                                                        \
       NOT the MaxReadRequest encoding above, which is the one every other read                                                    \
       request field in the map uses. There 000b is Reserved; here 000b and                                                        \
       001b BOTH mean 64 bytes and no encoding is reserved. Reusing the other                                                      \
       table would print "Reserved" for a perfectly legal advertisement. --- */                                                    \
    X( TargetMaxReadRequest, 0x0, "64 bytes" )                                                                                     \
    X( TargetMaxReadRequest, 0x1, "64 bytes" )                                                                                     \
    X( TargetMaxReadRequest, 0x2, "128 bytes" )                                                                                    \
    X( TargetMaxReadRequest, 0x3, "256 bytes" )                                                                                    \
    X( TargetMaxReadRequest, 0x4, "512 bytes" )                                                                                    \
    X( TargetMaxReadRequest, 0x5, "1024 bytes" )                                                                                   \
    X( TargetMaxReadRequest, 0x6, "2048 bytes" )                                                                                   \
    X( TargetMaxReadRequest, 0x7, "4096 bytes" )                                                                                   \
    /* --- Maximum WAIT STATE Allowed, offset 08h bits 15:12, p.96.                                                                \
       "This is a 1-based field in the granularity of byte time. When '0', it                                                      \
       indicates a value of 16 byte time." So zero is the largest value here,                                                      \
       not the smallest -- it is enumerated rather than computed so that the                                                       \
       wrap-around is visible to a reader checking it against the page. --- */                                                     \
    X( WaitState, 0x0, "16 byte times" )                                                                                           \
    X( WaitState, 0x1, "1 byte time" )                                                                                             \
    X( WaitState, 0x2, "2 byte times" )                                                                                            \
    X( WaitState, 0x3, "3 byte times" )                                                                                            \
    X( WaitState, 0x4, "4 byte times" )                                                                                            \
    X( WaitState, 0x5, "5 byte times" )                                                                                            \
    X( WaitState, 0x6, "6 byte times" )                                                                                            \
    X( WaitState, 0x7, "7 byte times" )                                                                                            \
    X( WaitState, 0x8, "8 byte times" )                                                                                            \
    X( WaitState, 0x9, "9 byte times" )                                                                                            \
    X( WaitState, 0xA, "10 byte times" )                                                                                           \
    X( WaitState, 0xB, "11 byte times" )                                                                                           \
    X( WaitState, 0xC, "12 byte times" )                                                                                           \
    X( WaitState, 0xD, "13 byte times" )                                                                                           \
    X( WaitState, 0xE, "14 byte times" )                                                                                           \
    X( WaitState, 0xF, "15 byte times" )

// ---------------------------------------------------------------------------
//  FIELDS
//
//  X( OFFSET, HIGH, LOW, NAME, ENUM, ZERO_BASED, ACCESS, DEFAULT, VALUE )
//
//  ENUM        the value table above, or None for a plain number
//  ZERO_BASED  true where the specification says the field is a 0-based count
//  ACCESS      RO, RW, or RwOrRo where the page prints "RW / RO"
//  DEFAULT     Value  -- the page gives a number, in VALUE
//              HwInit -- hardware initialised, no value the spec can state
//              None   -- the page leaves the Default column empty
//  VALUE       the numeric default; 0 and meaningless unless DEFAULT is Value
//
//  WHY THE DEFAULT COLUMN IS LOAD BEARING, and not just documentation: it is
//  the state machine's starting point. A capture begins with the link out of
//  eSPI Reset#, and what the bus looks like at that moment is exactly this
//  column -- Single I/O, CRC checking off, peripheral channel enabled and
//  every other channel disabled. A decoder that does not know the reset state
//  cannot decode the first transaction of a capture, because nothing on the
//  wire announces it. See ConfigResetValue() for the assembled state.
//
//  ONE ODDITY, recorded rather than smoothed over: the Default column for
//  Flash Block Erase Size (040h bits 4:2, p.103) prints "01b" -- two bits for
//  a three bit field. Read as 001b it is 4 Kbytes, which is the only value
//  that makes sense next to the rest of the table, and that is how it is
//  transcribed. Flagged because it is the spec being sloppy, not us.
//
//  Reserved spans are listed so that every bit of the DWord is accounted for.
//  They are not rendered unless they are nonzero, which the specification
//  forbids -- see §6.2 and each register's Reserved rows.
// ---------------------------------------------------------------------------

#define ESPI_CONFIG_FIELD_TABLE( X )                                                                                               \
    /* --- 004h Device Identification, p.94 --- */                                                                                 \
    X( 0x004, 31, 8, "Reserved", None, false, RO, Value, 0 )                                                                       \
    X( 0x004, 7, 0, "Version ID", None, false, RO, Value, 0x01 )                                                                   \
    /* --- 008h General Capabilities and Configurations, pp.94-96 --- */                                                           \
    X( 0x008, 31, 31, "CRC Checking Enable", None, false, RW, Value, 0 )                                                           \
    X( 0x008, 30, 30, "Response Modifier Enable", None, false, RW, Value, 0 )                                                      \
    X( 0x008, 29, 29, "RTC-Integrated-BMC", None, false, RO, HwInit, 0 )                                                           \
    X( 0x008, 28, 28, "Alert Mode", None, false, RW, Value, 0 )                                                                    \
    X( 0x008, 27, 26, "I/O Mode Select", IoModeSelect, false, RW, Value, 0 )                                                       \
    X( 0x008, 25, 24, "I/O Mode Support", IoModeSupport, false, RO, None, 0 )                                                      \
    X( 0x008, 23, 23, "Open Drain Alert# Select", None, false, RW, None, 0 )                                                       \
    X( 0x008, 22, 20, "Operating Frequency", Frequency, false, RW, Value, 0 )                                                      \
    X( 0x008, 19, 19, "Open Drain Alert# Supported", None, false, RO, HwInit, 0 )                                                  \
    X( 0x008, 18, 16, "Maximum Frequency Supported", Frequency, false, RO, HwInit, 0 )                                             \
    X( 0x008, 15, 12, "Maximum WAIT STATE Allowed", WaitState, false, RW, Value, 0 )                                               \
    X( 0x008, 11, 8, "Reserved", None, false, RO, Value, 0 )                                                                       \
    X( 0x008, 7, 0, "Channel Supported", None, false, RO, HwInit, 0 )                                                              \
    /* --- 010h Channel 0 (Peripheral), pp.97-98 --- */                                                                            \
    X( 0x010, 31, 15, "Reserved", None, false, RO, Value, 0 )                                                                      \
    X( 0x010, 14, 12, "Peripheral Channel Maximum Read Request Size", MaxReadRequest, false, RW, Value, 1 )                        \
    X( 0x010, 11, 11, "Reserved", None, false, RO, Value, 0 )                                                                      \
    X( 0x010, 10, 8, "Peripheral Channel Maximum Payload Size Selected", MaxPayload, false, RW, Value, 1 )                         \
    X( 0x010, 7, 7, "Reserved", None, false, RO, Value, 0 )                                                                        \
    X( 0x010, 6, 4, "Peripheral Channel Maximum Payload Size Supported", MaxPayload, false, RO, HwInit, 0 )                        \
    X( 0x010, 3, 3, "Reserved", None, false, RO, Value, 0 )                                                                        \
    X( 0x010, 2, 2, "Bus Master Enable", None, false, RW, Value, 0 )                                                               \
    X( 0x010, 1, 1, "Peripheral Channel Ready", None, false, RO, Value, 0 )                                                        \
    X( 0x010, 0, 0, "Peripheral Channel Enable", None, false, RW, Value, 1 )                                                       \
    /* --- 020h Channel 1 (Virtual Wire), p.99 --- */                                                                              \
    X( 0x020, 31, 22, "Reserved", None, false, RO, Value, 0 )                                                                      \
    X( 0x020, 21, 16, "Operating Maximum Virtual Wire Count", None, true, RW, Value, 0 )                                           \
    X( 0x020, 15, 14, "Reserved", None, false, RO, Value, 0 )                                                                      \
    X( 0x020, 13, 8, "Maximum Virtual Wire Count Supported", None, true, RO, HwInit, 0 )                                           \
    X( 0x020, 7, 2, "Reserved", None, false, RO, Value, 0 )                                                                        \
    X( 0x020, 1, 1, "Virtual Wire Channel Ready", None, false, RO, Value, 0 )                                                      \
    X( 0x020, 0, 0, "Virtual Wire Channel Enable", None, false, RW, Value, 0 )                                                     \
    /* --- 030h Channel 2 (OOB Message), p.100 --- */                                                                              \
    X( 0x030, 31, 11, "Reserved", None, false, RO, Value, 0 )                                                                      \
    X( 0x030, 10, 8, "OOB Message Channel Maximum Payload Size Selected", MaxPayload, false, RW, Value, 1 )                        \
    X( 0x030, 7, 7, "Reserved", None, false, RO, Value, 0 )                                                                        \
    X( 0x030, 6, 4, "OOB Message Channel Maximum Payload Size Supported", MaxPayload, false, RO, HwInit, 0 )                       \
    X( 0x030, 3, 2, "Reserved", None, false, RO, Value, 0 )                                                                        \
    X( 0x030, 1, 1, "OOB Message Channel Ready", None, false, RO, Value, 0 )                                                       \
    X( 0x030, 0, 0, "OOB Message Channel Enable", None, false, RW, Value, 0 )                                                      \
    /* --- 040h Channel 3 (Flash Access), pp.101-103 --- */                                                                        \
    X( 0x040, 31, 24, "RPMC OP1 Opcode on the 1st RPMC Flash device", None, false, RO, HwInit, 0 )                                 \
    X( 0x040, 23, 20, "RPMC Counter on the 1st RPMC Flash device", None, true, RO, HwInit, 0 )                                     \
    X( 0x040, 19, 18, "Reserved", None, false, RO, Value, 0 )                                                                      \
    X( 0x040, 17, 16, "Flash Sharing Capability Supported", FlashSharingCap, false, RO, HwInit, 0 )                                \
    X( 0x040, 15, 15, "Reserved", None, false, RO, Value, 0 )                                                                      \
    X( 0x040, 14, 12, "Flash Access Channel Maximum Read Request Size", MaxReadRequest, false, RW, Value, 1 )                      \
    X( 0x040, 11, 11, "Flash Sharing Mode", FlashSharingMode, false, RwOrRo, HwInit, 0 )                                           \
    X( 0x040, 10, 8, "Flash Access Channel Maximum Payload Size Selected", MaxPayload, false, RW, Value, 1 )                       \
    X( 0x040, 7, 5, "Flash Access Channel Maximum Payload Size Supported", MaxPayload, false, RO, HwInit, 0 )                      \
    X( 0x040, 4, 2, "Flash Block Erase Size", FlashBlockErase, false, RW, Value, 1 )                                               \
    X( 0x040, 1, 1, "Flash Access Channel Ready", None, false, RO, Value, 0 )                                                      \
    X( 0x040, 0, 0, "Flash Access Channel Enable", None, false, RW, Value, 0 )                                                     \
    /* --- 044h Channel 3 Capabilities and Configurations 2, pp.104-105 --- */                                                     \
    X( 0x044, 31, 24, "Reserved", None, false, RO, Value, 0 )                                                                      \
    X( 0x044, 23, 22, "Number of Target Attached Flash RPMC flash devices", RpmcDeviceCount, false, RO, HwInit, 0 )                \
    X( 0x044, 21, 16, "Target RPMC Supported", None, false, RO, HwInit, 0 )                                                        \
    X( 0x044, 15, 8, "Target Flash Erase Block Size", None, false, RO, HwInit, 0 )                                                 \
    X( 0x044, 7, 3, "Reserved", None, false, RO, Value, 0 )                                                                        \
    X( 0x044, 2, 0, "Target Maximum Read Request Size Supported", TargetMaxReadRequest, false, RO, HwInit, 0 )                     \
    /* --- 048h Channel 3 Capabilities and Configurations 3, p.106 --- */                                                          \
    X( 0x048, 31, 24, "RPMC OP1 Opcode on the 2nd RPMC Flash device", None, false, RO, HwInit, 0 )                                 \
    X( 0x048, 23, 20, "RPMC Counter on the 2nd RPMC Flash device", None, true, RO, HwInit, 0 )                                     \
    X( 0x048, 19, 0, "Reserved", None, false, RO, Value, 0 )                                                                       \
    /* --- 04Ch Channel 3 Capabilities and Configurations 4, p.106.                                                                \
       The 4th device is in the high half and the 3rd in the low half, which is                                                    \
       the order the page prints them in and not the order a reader expects. --- */                                                \
    X( 0x04C, 31, 24, "RPMC OP1 Opcode on the 4th RPMC Flash device", None, false, RO, HwInit, 0 )                                 \
    X( 0x04C, 23, 20, "RPMC Counter on the 4th RPMC Flash device", None, true, RO, HwInit, 0 )                                     \
    X( 0x04C, 19, 16, "Reserved", None, false, RO, Value, 0 )                                                                      \
    X( 0x04C, 15, 8, "RPMC OP1 Opcode on the 3rd RPMC Flash device", None, false, RO, HwInit, 0 )                                  \
    X( 0x04C, 7, 4, "RPMC Counter on the 3rd RPMC Flash device", None, true, RO, HwInit, 0 )                                       \
    X( 0x04C, 3, 0, "Reserved", None, false, RO, Value, 0 )

// ---------------------------------------------------------------------------
//  CHANNEL SUPPORTED -- offset 08h bits 7:0, p.96.
//
//  A bit field rather than an encoded value: each bit set means that channel
//  is supported by the target.
// ---------------------------------------------------------------------------

// X( BIT, NAME )
#define ESPI_CHANNEL_SUPPORTED_TABLE( X )                                                                                          \
    X( 0, "Peripheral Channel" )                                                                                                   \
    X( 1, "Virtual Wire Channel" )                                                                                                 \
    X( 2, "OOB Message Channel" )                                                                                                  \
    X( 3, "Flash Access Channel" )

// Bits 7:4 are "Reserved for platform specific channels" (p.96), so they are
// not an error when set -- only unnamed.
#define ESPI_CHANNEL_SUPPORTED_PLATFORM_MASK 0xF0u

// ---------------------------------------------------------------------------
//  I/O MODE SELECT, AS A MODE RATHER THAN AS A LABEL -- offset 08h bits 27:26,
//  §6.2.1.3, p.95.
//
//  The same four rows the IoModeSelect group of ESPI_CONFIG_ENUM_TABLE already
//  carries, stated a second time in the form the session state machine needs:
//  an espi::IoMode the sampler can be switched to, rather than a string to
//  print. Two transcriptions of one page is a thing worth being uneasy about,
//  so tests/test_link.cpp asserts row for row that the two agree -- a table
//  edited on one side and not the other is a failure, not a divergence nobody
//  notices.
//
//  X( ENCODING, MODE, RESERVED )
//
//  RESERVED marks the row the page prints as Reserved. That row's MODE column
//  is not a value the specification gives and nothing reads it: a reserved
//  encoding leaves the session where it was, because a controller writing 11b
//  has told us nothing about what mode it is about to talk in and picking one
//  would be a guess dressed as a decode.
//
//  WHAT THIS FIELD DOES TO A CAPTURE. From p.95: "eSPI controller programs
//  this field to enable the appropriate mode of operation, which will take
//  effect at the deassertion edge of the Chip Select#." §5.1, p.86, says the
//  same from the link layer's side and adds what happens to the transaction
//  that carries it -- "The SET_CONFIGURATION is completed with the current
//  mode of operation" -- so the command, its response and its CRC are all
//  still in the old mode, and only the transaction after it is not.
//
//  Also from p.95, and not acted on here: "The I/O Mode configured in this
//  field must be supported by both the controller and the target." What the
//  target supports is the neighbouring I/O Mode Support field, which only ever
//  appears in a GET_CONFIGURATION response -- so checking a selection against
//  it means remembering a different transaction, and nothing in this tree does
//  that yet.
// ---------------------------------------------------------------------------

// X( ENCODING, MODE, RESERVED )
#define ESPI_IO_MODE_SELECT_TABLE( X )                                                                                             \
    X( 0x0, Single, false )                                                                                                        \
    X( 0x1, Dual, false )                                                                                                          \
    X( 0x2, Quad, false )                                                                                                          \
    X( 0x3, Single, true ) /* Reserved -- the mode column is unread on this row */

// The register the two fields above live in. It repeats the Start column of
// Table 21's "General Capabilities and Configurations" row rather than stating
// anything new, and the code needs it as a number: the whole of the session
// state machine hangs off recognising this one address.
#define ESPI_GENERAL_CONFIG_OFFSET 0x008u

// ---------------------------------------------------------------------------
//  TARGET FLASH ERASE BLOCK SIZE -- offset 44h bits 15:8, p.105.
//
//  The second bit field in the map, and the only other field that has to be
//  rendered a bit at a time: "This field indicates the size of the erase
//  commands the controller can issue. If multiple bits are set then the
//  controller is allowed to issue an erase using any of the indicated sizes."
//
//  X( BIT, NAME )   -- BIT is numbered within the field, so bit 0 is register
//                      bit 8.
//
//  IT IS NOT THE SAME THING AS THE FLASH BLOCK ERASE SIZE FIELD at 040h bits
//  4:2, however similar the names look. That one is an encoded value the
//  controller writes to pick a size; this one is a capability mask the target
//  advertises, and the bit positions are not the encodings. The page reserves
//  bits 0, 1, 3 and 4 -- a gap in the middle that no encoded field would have.
//
//  "This field is only applicable when target attached flash sharing scheme is
//  selected" (p.105).
// ---------------------------------------------------------------------------

// X( BIT, NAME )
#define ESPI_TARGET_ERASE_BLOCK_TABLE( X )                                                                                         \
    X( 2, "4 Kbytes" )                                                                                                             \
    X( 5, "32 Kbytes" )                                                                                                            \
    X( 6, "64 Kbytes" )                                                                                                            \
    X( 7, "128 Kbytes" )

// Bits 0, 1, 3 and 4 of the field are printed as Reserved on p.105.
#define ESPI_TARGET_ERASE_BLOCK_RESERVED_MASK 0x1Bu

// The three addressing rules from §3.7, pp.37-38. See the banner above for why
// the upper bits are reported rather than masked away.
//
//   SELECT   bits that choose a register -- the 4 KB space
//   UPPER    bits the controller must drive to zero, and the target ignores
//   DWORD    bits hard-wired to 00, so a register is addressed at its base
#define ESPI_CONFIG_ADDRESS_SELECT_MASK 0x0FFFu
#define ESPI_CONFIG_ADDRESS_UPPER_MASK 0xF000u
#define ESPI_CONFIG_ADDRESS_DWORD_MASK 0x0003u

#endif // ESPI_TABLE_CONFIG_REGISTERS_H
