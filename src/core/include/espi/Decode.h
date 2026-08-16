#ifndef ESPI_DECODE_H
#define ESPI_DECODE_H

#include "espi/ByteStream.h"

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

struct Field
{
    std::string name;       // "Index", "SLAVE_BOOT_LOAD_STATUS", "CRC"
    std::string text;       // formatted for humans: "0x05", "64 bytes", "valid"
    uint64_t raw = 0;       // the underlying value
    uint8_t bit_width = 8;  // significant bits in `raw`, for formatting
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

// One chip-select-delimited transaction.
struct Transaction
{
    std::vector<Field> fields; // top level: Command, TAR, Response
    ByteSpan span{};
    bool truncated = false;    // chip select deasserted mid-packet

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
