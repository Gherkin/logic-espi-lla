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
//
//  ADDRESSING. Registers are DWord granular and only the low 12 bits of the
//  16-bit address are decoded -- the target has a 4 KB register space (§3.7,
//  p.37). Offsets 800h-FFFh are platform specific and 050h-7FFh are reserved,
//  so neither is transcribed here; both report as an untranscribed address
//  rather than being guessed at.
//
//  NOT TRANSCRIBED YET, deliberately: Channel 3 Capabilities and
//  Configurations 2, 3 and 4 at offsets 044h, 048h and 04Ch. They carry RPMC
//  detail for the 2nd-4th flash devices and belong with the flash channel
//  work. A GET_CONFIGURATION of those offsets reports a gap.
//
//  WHY THE ENABLE AND READY BITS MATTER TO THE DECODER ITSELF. This is not
//  just labelling: a SET_CONFIGURATION to offset 08h that is accepted changes
//  the I/O mode and whether CRC checking is on, and it takes effect at the
//  *deassertion edge of Chip Select#* (§6.2, p.92, and the I/O Mode Select
//  field on p.95). An analyzer that ignores this decodes the first few hundred
//  microseconds of a capture and then produces garbage.
// ---------------------------------------------------------------------------

// X( OFFSET, NAME )
#define ESPI_CONFIG_REGISTER_TABLE( X )                                                                                            \
    X( 0x004, "Device Identification" )                                                                                            \
    X( 0x008, "General Capabilities and Configurations" )                                                                          \
    X( 0x010, "Channel 0 Capabilities and Configurations" )                                                                        \
    X( 0x020, "Channel 1 Capabilities and Configurations" )                                                                        \
    X( 0x030, "Channel 2 Capabilities and Configurations" )                                                                        \
    X( 0x040, "Channel 3 Capabilities and Configurations" )

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
//  X( OFFSET, HIGH, LOW, NAME, ENUM, ZERO_BASED )
//
//  ENUM        the value table above, or None for a plain number
//  ZERO_BASED  true where the specification says the field is a 0-based count,
//              so the decoder can report both the raw value and what it means
//
//  Reserved spans are listed so that every bit of the DWord is accounted for.
//  They are not rendered unless they are nonzero, which the specification
//  forbids -- see §6.2 and each register's Reserved rows.
// ---------------------------------------------------------------------------

#define ESPI_CONFIG_FIELD_TABLE( X )                                                                                               \
    /* --- 004h Device Identification, p.94 --- */                                                                                 \
    X( 0x004, 31, 8, "Reserved", None, false )                                                                                     \
    X( 0x004, 7, 0, "Version ID", None, false )                                                                                    \
    /* --- 008h General Capabilities and Configurations, pp.94-96 --- */                                                           \
    X( 0x008, 31, 31, "CRC Checking Enable", None, false )                                                                         \
    X( 0x008, 30, 30, "Response Modifier Enable", None, false )                                                                    \
    X( 0x008, 29, 29, "RTC-Integrated-BMC", None, false )                                                                          \
    X( 0x008, 28, 28, "Alert Mode", None, false )                                                                                  \
    X( 0x008, 27, 26, "I/O Mode Select", IoModeSelect, false )                                                                     \
    X( 0x008, 25, 24, "I/O Mode Support", IoModeSupport, false )                                                                   \
    X( 0x008, 23, 23, "Open Drain Alert# Select", None, false )                                                                    \
    X( 0x008, 22, 20, "Operating Frequency", Frequency, false )                                                                    \
    X( 0x008, 19, 19, "Open Drain Alert# Supported", None, false )                                                                 \
    X( 0x008, 18, 16, "Maximum Frequency Supported", Frequency, false )                                                            \
    X( 0x008, 15, 12, "Maximum WAIT STATE Allowed", WaitState, false )                                                             \
    X( 0x008, 11, 8, "Reserved", None, false )                                                                                     \
    X( 0x008, 7, 0, "Channel Supported", None, false )                                                                             \
    /* --- 010h Channel 0 (Peripheral), pp.97-98 --- */                                                                            \
    X( 0x010, 31, 15, "Reserved", None, false )                                                                                    \
    X( 0x010, 14, 12, "Peripheral Channel Maximum Read Request Size", MaxReadRequest, false )                                       \
    X( 0x010, 11, 11, "Reserved", None, false )                                                                                    \
    X( 0x010, 10, 8, "Peripheral Channel Maximum Payload Size Selected", MaxPayload, false )                                        \
    X( 0x010, 7, 7, "Reserved", None, false )                                                                                      \
    X( 0x010, 6, 4, "Peripheral Channel Maximum Payload Size Supported", MaxPayload, false )                                        \
    X( 0x010, 3, 3, "Reserved", None, false )                                                                                      \
    X( 0x010, 2, 2, "Bus Master Enable", None, false )                                                                             \
    X( 0x010, 1, 1, "Peripheral Channel Ready", None, false )                                                                      \
    X( 0x010, 0, 0, "Peripheral Channel Enable", None, false )                                                                     \
    /* --- 020h Channel 1 (Virtual Wire), p.99 --- */                                                                              \
    X( 0x020, 31, 22, "Reserved", None, false )                                                                                    \
    X( 0x020, 21, 16, "Operating Maximum Virtual Wire Count", None, true )                                                          \
    X( 0x020, 15, 14, "Reserved", None, false )                                                                                    \
    X( 0x020, 13, 8, "Maximum Virtual Wire Count Supported", None, true )                                                           \
    X( 0x020, 7, 2, "Reserved", None, false )                                                                                      \
    X( 0x020, 1, 1, "Virtual Wire Channel Ready", None, false )                                                                     \
    X( 0x020, 0, 0, "Virtual Wire Channel Enable", None, false )                                                                    \
    /* --- 030h Channel 2 (OOB Message), p.100 --- */                                                                              \
    X( 0x030, 31, 11, "Reserved", None, false )                                                                                    \
    X( 0x030, 10, 8, "OOB Message Channel Maximum Payload Size Selected", MaxPayload, false )                                       \
    X( 0x030, 7, 7, "Reserved", None, false )                                                                                      \
    X( 0x030, 6, 4, "OOB Message Channel Maximum Payload Size Supported", MaxPayload, false )                                       \
    X( 0x030, 3, 2, "Reserved", None, false )                                                                                      \
    X( 0x030, 1, 1, "OOB Message Channel Ready", None, false )                                                                      \
    X( 0x030, 0, 0, "OOB Message Channel Enable", None, false )                                                                     \
    /* --- 040h Channel 3 (Flash Access), pp.101-103 --- */                                                                        \
    X( 0x040, 31, 24, "RPMC OP1 Opcode on the 1st RPMC Flash device", None, false )                                                 \
    X( 0x040, 23, 20, "RPMC Counter on the 1st RPMC Flash device", None, true )                                                     \
    X( 0x040, 19, 18, "Reserved", None, false )                                                                                    \
    X( 0x040, 17, 16, "Flash Sharing Capability Supported", FlashSharingCap, false )                                                \
    X( 0x040, 15, 15, "Reserved", None, false )                                                                                    \
    X( 0x040, 14, 12, "Flash Access Channel Maximum Read Request Size", MaxReadRequest, false )                                      \
    X( 0x040, 11, 11, "Flash Sharing Mode", FlashSharingMode, false )                                                               \
    X( 0x040, 10, 8, "Flash Access Channel Maximum Payload Size Selected", MaxPayload, false )                                       \
    X( 0x040, 7, 5, "Flash Access Channel Maximum Payload Size Supported", MaxPayload, false )                                       \
    X( 0x040, 4, 2, "Flash Block Erase Size", FlashBlockErase, false )                                                              \
    X( 0x040, 1, 1, "Flash Access Channel Ready", None, false )                                                                     \
    X( 0x040, 0, 0, "Flash Access Channel Enable", None, false )

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

// Only the low 12 bits of the configuration address are decoded (§3.7, p.37).
#define ESPI_CONFIG_ADDRESS_MASK 0x0FFFu

#endif // ESPI_TABLE_CONFIG_REGISTERS_H
