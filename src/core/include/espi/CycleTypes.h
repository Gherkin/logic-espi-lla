#ifndef ESPI_CYCLE_TYPES_H
#define ESPI_CYCLE_TYPES_H

#include "espi/Opcodes.h"

#include <cstddef>
#include <cstdint>

namespace espi
{

// The cycle type byte that opens a peripheral, OOB or flash packet, and the
// header layout behind it.
//
// Transcribed in src/core/tables/CycleTypes.h from base spec Table 5, pp.47-49
// and Figures 34-40, pp.53-56 -- that header is where the values live and what
// the QC worksheet checks.
//
// EVERY LOOKUP HERE IS KEYED BY CHANNEL. Table 5 note 3, p.49: "The
// combination of command opcode and cycle type encoding must be unique. There
// is no requirement that cycle type encodings must be unique across command
// opcodes." Byte 00h is Memory Read 32 on the peripheral channel and Flash
// Read on the flash channel, so an API that took the byte alone would have to
// pick one and be wrong half the time.

// The "Command Type" column of Table 5.
enum class CycleCommandType : uint8_t
{
    Posted,
    NonPosted,
    Completion,
};

// The "Direction" column. Section 4.1.1, p.46, defines the words: Up is target
// to controller, Down is controller to target. UpOrDown is the page's
// "Up/Down" -- the cycle type is legal in both directions, and which one a
// given packet is travelling in comes from the opcode, not from this byte.
enum class CycleDirection : uint8_t
{
    Up,
    Down,
    UpOrDown,
};

// Which packet format figure a cycle type points at.
enum class CycleLayout : uint8_t
{
    // --- peripheral channel, Figures 34-39, pp.53-55 ---
    MemoryRead32,
    MemoryRead64,
    MemoryWrite32,
    MemoryWrite64,
    Message,
    MessageWithData,
    CompletionWithData,
    CompletionWithoutData,

    // --- OOB message channel, Figure 45, p.73 ---
    OobMessage,

    // --- flash access channel, Figures 48-50, pp.75-76 ---
    //
    // The completions are drawn by Figure 49 exactly as Figure 39 draws the
    // peripheral ones, and section 4.2.4 states it: "The Flash Access channel
    // uses the same packet format as the eSPI Peripheral Channel transactions"
    // (p.75). They are separate values anyway so each row cites the one figure
    // it was checked against.
    FlashRead,
    FlashWrite,
    FlashErase,
    FlashRpmcOp1,
    FlashRpmcOp2,
    FlashCompletionWithData,
    FlashCompletionWithoutData,

    // The row's figure has not been read yet. This is a gap in the
    // transcription, not a gap in the specification, and it is a different
    // answer from either of those -- the decoder names the cycle type and
    // stops rather than inventing a header length.
    //
    // No row carries this now. It is kept because it is the mechanism that
    // keeps the next gap a gap instead of a guess.
    NotTranscribed,
};

// Which variable field a cycle type encoding carries, if any. Named rather
// than inferred from the mask so the decoder resolves the field the row
// actually has: three different fields live in three different bit ranges and
// two of them overlap.
enum class CycleVariable : uint8_t
{
    None,
    SplitCompletion, // P1P0 at bits [2:1] -- Table 5 note 1
    MessageRouting,  // r2r1r0 at bits [3:1] -- Table 5 note 5
    RpmcTarget,      // R1R0 at bits [6:5] -- Table 5 note 6
};

// How to read the 12-bit Length field of a header. Section 4.1.3, pp.50-51 --
// not stated by Table 5 and not visible in any figure.
enum class CycleLength : uint8_t
{
    OneBased,   // 1-based; a value of all zeros is 4 KB, not zero
    MustBeZero, // driven to zeros by the initiator, ignored by the receiver
    Reserved,   // Message cycle type: reserved, sent as all 0s

    // Flash Erase. Not a byte count: the field carries an erase block size
    // encoding, and which size a given encoding names depends on the flash
    // sharing scheme in operation, which never appears on the bus. Table 16,
    // p.81; section 4.1.3 p.50 forwards to 4.2.4.1 and 4.2.4.2 rather than
    // stating it.
    BlockErase,
};

// Whether the data bytes after a header have a transcribed structure.
enum class CyclePayload : uint8_t
{
    Opaque,      // data bytes with no structure this document gives
    SmbusPacket, // Figure 45, p.73 -- the tunneled SMBus block write
    RpmcOpcode,  // Figure 50, p.76 -- data byte 0 is the RPMC OP1 opcode
};

struct CycleTypeInfo
{
    const char* name = nullptr;
    uint8_t encoding = 0; // the byte with every variable field zeroed
    uint8_t mask = 0xFF;  // 0 bits wherever the encoding has a variable one
    ChannelId channel = ChannelId::Peripheral;
    CycleDirection direction = CycleDirection::UpOrDown;
    CycleCommandType command_type = CycleCommandType::Posted;
    CycleLayout layout = CycleLayout::NotTranscribed;
    CycleVariable variable = CycleVariable::None;
};

// Look up a cycle type byte on a channel. Returns false when the channel
// defines no cycle type matching the byte -- the decoder reports that rather
// than borrowing another channel's meaning.
bool LookupCycleType( ChannelId channel, uint8_t cycle_type, CycleTypeInfo* out );

// The value of a row's variable field, extracted from the byte. Zero for a row
// whose CycleVariable is None.
uint8_t CycleVariableValue( const CycleTypeInfo& info, uint8_t cycle_type );

// Table 5 notes 1, 5 and 6. Each returns null for an encoding the note leaves
// Reserved, so an undefined value reports as undefined instead of borrowing
// the text of a neighbouring row.
const char* SplitCompletionText( uint8_t p1p0 );
const char* MessageRoutingText( uint8_t r2r1r0 );
const char* RpmcTargetText( uint8_t r1r0 );

// Table 5 note 2, p.49: on an Unsuccessful Completion without Data, P1 must
// always be 1, "as this is always the last or the only completion". True when
// a completion's split field violates that.
bool SplitCompletionViolatesNote2( const CycleTypeInfo& info, uint8_t cycle_type );

// The shape of one packet header, counted from the cycle type byte.
struct CycleHeaderLayout
{
    const char* figure = nullptr; // where it was read from, for the gap message
    uint8_t header_bytes = 0;     // including the cycle type byte
    uint8_t address_bytes = 0;    // 0, 4 or 8; most significant byte first
    bool has_message_code = false; // message code plus 4 message specific bytes
    bool has_payload = false;      // data bytes follow the header
    CycleLength length = CycleLength::OneBased;
    CyclePayload payload = CyclePayload::Opaque;
};

// Returns false for CycleLayout::NotTranscribed, which is the only layout with
// no figure behind it yet.
bool LookupCycleHeaderLayout( CycleLayout layout, CycleHeaderLayout* out );

// ---------------------------------------------------------------------------
//  The header fields shared by every non-short packet -- Figure 33, p.46.
// ---------------------------------------------------------------------------
uint8_t CycleTagOf( uint8_t byte1 );                            // bits [7:4]
uint16_t CycleLengthOf( uint8_t byte1, uint8_t byte2 );         // 12 bits
unsigned CycleLengthBits();

// Resolve a Length field to a byte count under section 4.1.3's rules. Only
// meaningful for CycleLength::OneBased -- the caller decides what to say about
// the other two, because there the field is not a count at all.
unsigned CycleResolvedLength( uint16_t length_field );

// ---------------------------------------------------------------------------
//  Short cycles -- Figures 35 and 37, pp.53-54. No cycle type byte, no tag and
//  no length field: the opcode carries all three.
// ---------------------------------------------------------------------------
unsigned CycleShortIoAddressBytes();
unsigned CycleShortMemoryAddressBytes();

// ---------------------------------------------------------------------------
//  Table 6, p.55 -- message codes.
// ---------------------------------------------------------------------------
struct MessageCodeInfo
{
    uint8_t code = 0;
    const char* name = nullptr;
    const char* description = nullptr;
    uint8_t routing = 0;
    CycleDirection direction = CycleDirection::Up;

    // Whether the four message specific bytes of Figure 38 have a transcribed
    // layout behind them. A named code whose fields nobody has read is a
    // different answer from a code Table 6 does not list at all.
    bool fields_transcribed = false;
};

// False for a code Table 6 does not list. The base specification defines
// exactly one, so that is an ordinary outcome rather than an error.
bool LookupMessageCode( uint8_t code, MessageCodeInfo* out );

// ---------------------------------------------------------------------------
//  Figure 40 and Table 7, pp.56-57 -- the LTR message's four message specific
//  bytes.
// ---------------------------------------------------------------------------
struct LtrMessage
{
    bool requirement = false; // RQ, byte 4 bit 7
    uint8_t reserved = 0;     // byte 4 bits [6:5]
    uint8_t scale = 0;        // LS[2:0], byte 4 bits [4:2]
    uint16_t value = 0;       // LV[9:0], byte 4 bits [1:0] then byte 5
};

// Decode the two LTR bytes. Bytes 6 and 7 are Reserved and are not part of
// this; the caller reports them.
LtrMessage DecodeLtrMessage( uint8_t byte4, uint8_t byte5 );

// The Latency Scale multiplier in nanoseconds, and its printed form. Both
// report failure for the Reserved encodings 110b and 111b rather than
// returning a multiplier the specification never gave.
bool LtrScaleNanoseconds( uint8_t scale, uint32_t* out );
const char* LtrScaleText( uint8_t scale );

// ---------------------------------------------------------------------------
//  Table 16, p.81 -- the Flash Erase block size encodings.
//
//  Two answers for one encoding, because Table 16 prints two lists in one cell
//  and the flash sharing scheme that picks between them is set in a
//  configuration register, not carried in the packet. A decoder that reported
//  only one of them would be right half the time and silent about it.
// ---------------------------------------------------------------------------
struct FlashEraseSize
{
    // Null where Table 16 prints Reserved for that scheme. Both null means the
    // encoding is Reserved either way, which is every value above 5h.
    const char* target_attached = nullptr;
    const char* controller_attached = nullptr;
};

// The two readings of a Flash Erase Length field. Both members are null for an
// encoding Table 16 reserves on both sides.
FlashEraseSize LookupFlashEraseSize( uint16_t length_field );

// ---------------------------------------------------------------------------
//  Figure 45, p.73 and section 4.2.3 -- the OOB tunneled SMBus packet.
//
//  Every field here is inside the OOB packet's *data*, not its header: the
//  header is the ordinary three bytes and the SMBus packet starts at byte 3.
// ---------------------------------------------------------------------------
struct SmbusPacketInfo
{
    uint8_t address = 0;      // SMBus Target Address, byte 3 bits [7:1]
    uint8_t address_bit0 = 0; // byte 3 bit 0, drawn as a literal 0
    uint8_t command = 0;      // SMBus Command Opcode, byte 4
    uint8_t byte_count = 0;   // SMBus Byte Count, byte 5

    // Length - 3 - byte_count. The specification allows 0 or 1; anything else
    // is a packet whose two length statements disagree.
    int pec_bytes = 0;
    bool consistent = false; // pec_bytes is 0 or 1
};

// Resolve the three SMBus header bytes against the OOB header's Length field.
// `length` is the resolved byte count of the whole OOB data region.
SmbusPacketInfo DecodeSmbusPacket( unsigned length, uint8_t byte3, uint8_t byte4, uint8_t byte5 );

// Bytes the SMBus header occupies inside the OOB data region.
unsigned SmbusHeaderBytes();

// Byte 3 bit 0, which Figure 45 draws as a literal 0.
uint8_t SmbusAddressBit0Expected();

// How wide the SMBus Target Address field is. Figure 45 gives it bits [7:1],
// so 7 -- and the shift alone cannot say that, because shifting a byte right by
// one already fits in seven bits. The width has to be read out of the table for
// anything to depend on it.
unsigned SmbusAddressBits();

// An SMBus Command Opcode the specification names. Returns false for a code it
// does not -- the base spec names exactly one, MCTP.
struct SmbusCommandInfo
{
    uint8_t code = 0;
    const char* name = nullptr;
    uint8_t header_bytes = 0; // bytes the embedded protocol spends on its header
};
bool LookupSmbusCommand( uint8_t code, SmbusCommandInfo* out );

// ---------------------------------------------------------------------------
//  Figure 46, p.74 -- the five MCTP header bytes.
//
//  The names are eSPI's, read off the figure. The meanings are MCTP's and this
//  document never gives them, so the decoder prints a name and a number and
//  makes no claim about what the number signifies.
// ---------------------------------------------------------------------------
struct MctpHeaderField
{
    uint8_t byte = 0; // offset within the MCTP header, so 0 is packet byte 6
    uint8_t high = 0;
    uint8_t low = 0;
    const char* name = nullptr;
    uint32_t value = 0;
};

// Split the five MCTP header bytes into named fields, most significant first.
// Returns how many fields there are; writes up to `capacity`.
size_t DecodeMctpHeader( const uint8_t bytes[ 5 ], MctpHeaderField* out, size_t capacity );

// Byte 0 bit 0 of the MCTP header, which Figure 46 draws as a literal 1.
uint8_t MctpSourceBit0( uint8_t byte0 );
uint8_t MctpSourceBit0Expected();

// ---------------------------------------------------------------------------
//  Names for the enumerations above, so the decoder's wording lives next to
//  the table it came from rather than being spelled out at each use.
// ---------------------------------------------------------------------------
const char* CycleCommandTypeName( CycleCommandType type );
const char* CycleDirectionText( CycleDirection direction );

// How many cycle type rows are transcribed, and the row at an index. Used by
// tests to walk the whole table; a table checked only where the decoder
// happens to reach it is a table with untested rows.
size_t CycleTypeCount();
const CycleTypeInfo& CycleTypeAt( size_t index );

} // namespace espi

#endif // ESPI_CYCLE_TYPES_H
