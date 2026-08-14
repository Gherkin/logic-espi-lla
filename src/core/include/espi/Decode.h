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

    Field& Add( Field child )
    {
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
