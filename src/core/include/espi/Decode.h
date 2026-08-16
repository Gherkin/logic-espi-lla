#ifndef ESPI_DECODE_H
#define ESPI_DECODE_H

#include "espi/ByteStream.h"
#include "espi/ConfigRegisters.h"

#include <string>
#include <vector>

namespace espi
{

enum class Severity : uint8_t
{
    Info,
    Warning,
    Error,
};

// ---------------------------------------------------------------------------
//  The neutral decode record.
//
//  A tree, not a flat list, because the goal is that no byte is ever shown as
//  an unexplained number. A status byte is one field with sixteen children; a
//  virtual wire packet is an index field whose children are the individual
//  wires with their valid bits resolved.
//
//  This type is the whole reason the core is worth separating from the Saleae
//  shell. It is what T1 tests assert against -- no SDK, no waveform, no
//  FrameV2 stub needed to check that a decode is correct and complete. The
//  shell's only job is turning this into Frames and FrameV2 key/values.
// ---------------------------------------------------------------------------

// Whether a field carries a value or explains one.
//
// Almost every field has a `raw` worth showing. A few do not: the "Masked" note
// on a virtual wire packet lists the wires whose valid bit is clear, and is
// built with a raw of 0 because there is no value to carry. A reader cannot
// tell the two apart from the outside, and a presentation layer that guesses
// prints "Masked=0" beside "TARGET_BOOT_LOAD_STATUS=high" as though both were
// wire states.
enum class FieldKind : uint8_t
{
    Value,
    Note,
};

struct Field
{
    std::string name;       // "Index", "SLAVE_BOOT_LOAD_STATUS", "CRC"

    // Formatted for humans, as "<value>  <meaning>" -- two spaces, either half
    // may be absent: "0x05  GET_VWIRE", "0x88", "2 clocks". Render() prepends
    // the name and nothing else, so this is the whole of a decoded line, and
    // the split is the only thing a bubble has to work with when it needs to
    // show the meaning without the number.
    std::string text;

    uint64_t raw = 0;       // the underlying value
    uint8_t bit_width = 8;  // significant bits in `raw`, for formatting
    FieldKind kind = FieldKind::Value;
    Severity severity = Severity::Info;
    ByteSpan span{};
    std::vector<Field> children;

    Field() = default;
    Field( std::string n, std::string t, uint64_t r, uint8_t w, ByteSpan s )
        : name( std::move( n ) ), text( std::move( t ) ), raw( r ), bit_width( w ), span( s )
    {
    }

    // Adding a child widens this field's span to cover it.
    //
    // The span is what the shell turns into a Frame's start and end, so a
    // container that claims fewer samples than the bytes it holds would draw
    // the wrong bubble. Three containers were in that state -- Virtual Wire
    // Packet, SMBus Packet and Message Code -- because each is constructed
    // with its first byte's span and then has later bytes added to it.
    //
    // Done here rather than at each construction site because this is the only
    // way a child is ever attached, so the invariant holds for containers
    // nobody has written yet. Widening is a no-op for the common case where a
    // child explains its parent's own bytes and carries the parent's span --
    // status bits, register fields -- which is what keeps the shell's
    // ChildrenAreSeparateBytes() test meaning the same thing as before.
    //
    // A child with no span of its own does not widen anything: WAIT_STATE runs
    // and error fields are built with a default ByteSpan on purpose.
    //
    // tests/support/FieldInvariants.h checks the result on every fixture.
    Field& Add( Field child )
    {
        if( child.span.last > child.span.first )
            span = ( span.last > span.first ) ? Merge( span, child.span ) : child.span;
        children.push_back( std::move( child ) );
        return children.back();
    }

    // Deep search by name, for tests that want one field out of a tree
    // without asserting on the whole rendering.
    const Field* Find( const std::string& name ) const;
};

// ---------------------------------------------------------------------------
//  WHAT A TRANSACTION DOES TO THE SESSION
//
//  Two commands change how the transactions *after* them have to be decoded,
//  and both land on the same edge -- the deassertion of the chip select that
//  carried them, not the byte that stated them:
//
//    - a SET_CONFIGURATION to General Capabilities and Configurations that the
//      target ACCEPTs. §5.1, p.86: "The SET_CONFIGURATION is completed with the
//      current mode of operation. The new mode of operation will only take
//      effect at the deassertion edge of the Chip Select#." §6.2, pp.92-93,
//      says the same of any register write, and §5.2, p.90, of CRC checking.
//
//    - an In-band RESET, which returns that one register -- and only that one
//      -- to its reset default. §8.3.2, p.123: "Wait until CS# deassertion and
//      assert the in-band reset internally at the CS# deassertion edge."
//
//  And a third case that changes nothing while leaving nothing known. §8.3.2,
//  p.122, on a SET_CONFIGURATION that does not get an ACCEPT: "As the
//  transaction does not complete successfully, it is uncertain on the state of
//  the interface settings after the error." The specification's own answer to
//  that is the In-band RESET this section defines.
//
//  BOTH VALID CASES CARRY THE WHOLE REGISTER, which is why this record states
//  the resulting settings outright rather than a change against what came
//  before. A decoder that had to know the previous state to describe the new
//  one would be a decoder that could not decode a fixture on its own.
//
//  Carried as a struct rather than left in the rendered text: a state machine
//  that scraped field names would break the moment one was reworded, and would
//  break silently.
// ---------------------------------------------------------------------------
enum class SessionChange : uint8_t
{
    None,
    GeneralConfigWritten,   // an ACCEPTed write of 008h; `config` is the result
    GeneralConfigUncertain, // a write of 008h that did not complete
    InbandReset,            // 008h back to its reset default; `config` is that
};

struct SessionUpdate
{
    SessionChange change = SessionChange::None;

    // The settings in force from the deassertion edge onward. Meaningful for
    // GeneralConfigWritten and InbandReset; untouched for the other two.
    GeneralConfig config{};
};

// One chip-select-delimited transaction.
struct Transaction
{
    std::vector<Field> fields; // top level: Command, TAR, Response
    ByteSpan span{};
    bool truncated = false;    // chip select deasserted mid-packet
    SessionUpdate session{};

    const Field* Find( const std::string& name ) const;
    bool HasError() const;
};

// ---------------------------------------------------------------------------
//  Canonical text rendering.
//
//  This is the comparison surface for T1 and T4: the decoder renders a
//  transaction, the test diffs it against a literal .expected file written by
//  a human (rule R2). The renderer only formats the tree -- every name and
//  value in the output comes from the decoder, so a mis-transcribed table
//  shows up as a diff rather than being papered over.
//
//  Stable by contract: changing this format churns every .expected file, so
//  treat it as a format with a version, not an implementation detail.
// ---------------------------------------------------------------------------
std::string Render( const Transaction& transaction );
std::string Render( const Field& field, int indent = 0 );

} // namespace espi

#endif // ESPI_DECODE_H
