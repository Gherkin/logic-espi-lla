#ifndef ESPI_TEST_CAPTURE_DUMP_H
#define ESPI_TEST_CAPTURE_DUMP_H

// A parser for tests/vectors/espi_dump.txt -- a third-party decoder's export of
// real hardware, and the only fixture in this tree produced by an
// implementation unrelated to ours.
//
// IT HAS NO PROTOCOL KNOWLEDGE, for the same reason FixtureByteSource has none
// (rule R1). It reads one thing off each row -- the label -- and recognises
// four kinds of it and nothing else:
//
//   IDLE           chip select is deasserted -- close the current frame
//   TURN (xx)      the turn-around -- everything before it is the command
//                  phase, everything after it is the response phase
//   <name> (HH)    one byte on the bus, value HH, which the export calls <name>
//   (empty)        no label at all. The export's last row is like this, with
//                  chip select deasserted, and it closes the last frame.
//                  Accepted on the final row and rejected anywhere else.
//
// THE BYTE NAMES ARE KEPT, NOT DISCARDED, AND THAT IS THE POINT OF THE FILE.
// `ADDR`, `CRC`, `STS`, `Data`, `Length`, `Index`, `RESP` and the opcode names
// are another decoder's field-by-field interpretation of 732 bytes of real
// traffic, which is the largest body of independent evidence this project has.
// `DumpFrame::bytes` carries every one of them, and `test_capture.cpp` compares
// them against what our decoder attributes each byte to.
//
// The rule that matters is the direction, not the discarding:
//
//   BUILDING the fixture uses only CS# framing and the TURN row. The bytes
//   handed to the decoder are literal, in wire order, with no label anywhere
//   near them -- so nothing the other decoder thinks can steer ours.
//
//   COMPARING afterwards uses everything, names included. An oracle written by
//   somebody else is not a tautology; it is the only thing here that is not
//   our own reading of the specification handed back to us.
//
// An earlier version of this header threw the names away and called that
// independence. It was not: it was 732 rows of free evidence, deleted.
//
// The parenthesised value on a TURN row is not a bus byte. The turn-around is
// two clocks of nobody driving; the export prints a number for it and the
// number is dropped.
//
// COLUMN LAYOUT. The header row names five signals for eight signal columns, so
// which column carries what is not stated and docs/PLAN.md section 10 records
// that as unresolved. Nothing here depends on it. The one column this file
// touches is the last one before the label, and only as a cross-check: it is
// '1' on every IDLE row and '0' on every byte row but one, which is how an
// active-low chip select would behave. That is an inference from the data, not
// something the header states, so the disagreeing rows are counted and handed
// back rather than acted on. The framing comes from the labels either way.
//
// THE FILE OPENS MID-FRAME. Chip select is already asserted on the export's
// pre-trigger row at -0.00012 ms, so the first frame is a fragment of a
// transaction that began before the capture did, and its byte boundaries are
// wherever the other decoder started counting. That is the one row where the
// chip select column and the IDLE label disagree, and it is the reason nothing
// here treats a frame as necessarily whole.
//
// WHAT THIS BUYS. The export states where the command phase ends. Our decoder
// works that out from the opcode and the packet shape table, having never seen
// this file. FixtureByteSource::CommandFullyConsumed() then compares the two.
// That comparison is T4: an independent implementation's framing and length
// arithmetic against ours.

#include "support/FixtureByteSource.h"

#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace espi_test
{

// One byte row, with the name the export gave it. `frame.command`/
// `frame.response` hold the same values in the same order and nothing else --
// that vector is what the decoder is fed, and it must stay label-free.
struct DumpByte
{
    std::string label; // "ADDR", "CRC", "STS", "GET_CONFIGURATION", ...
    uint8_t value = 0;
    int line = 0;          // 1-based line in the dump
    bool response = false; // read after the TURN row
};

struct DumpFrame
{
    Frame frame;
    std::vector<DumpByte> bytes; // parallel to command ++ response, in wire order
    int first_line = 0;          // 1-based line in the dump holding this frame's first byte
    int last_line = 0;           // ... and its last byte
};

struct DumpStats
{
    size_t rows = 0;             // data rows, not counting the header
    size_t idle_rows = 0;        // CS# deasserted
    size_t turnaround_rows = 0;  // TURN
    size_t byte_rows = 0;        // rows carrying a byte on the bus
    bool ends_mid_frame = false; // the export stops with CS# still asserted

    // The export's very last row has an empty label. It carries no byte, and
    // its chip select column reads deasserted like every IDLE row, so it is
    // taken as the end of the last frame. Recorded rather than absorbed: it is
    // only accepted on the final row, and the test says so out loud.
    int unlabelled_row = 0;

    // Rows where the last signal column disagrees with the IDLE label, by line
    // number. See the column-layout note above.
    std::vector<int> chip_select_disagreements;
};

namespace detail
{

// "  0.00054 ms " -> 0.00054, and false for anything that is not a millisecond
// timestamp. The timestamp is not used for decoding; parsing it is how this
// file notices that it is looking at a differently shaped export.
inline bool ParseMilliseconds( const std::string& field, double* out )
{
    const char* begin = field.c_str();
    char* end = nullptr;
    const double value = std::strtod( begin, &end );
    if( end == begin )
        return false;
    while( *end == ' ' )
        ++end;
    if( end[ 0 ] != 'm' || end[ 1 ] != 's' )
        return false;
    for( const char* tail = end + 2; *tail != '\0'; ++tail )
        if( *tail != ' ' && *tail != '\r' )
            return false;
    *out = value;
    return true;
}

inline std::string Trim( const std::string& s )
{
    size_t begin = 0;
    size_t end = s.size();
    while( begin < end && ( s[ begin ] == ' ' || s[ begin ] == '\t' || s[ begin ] == '\r' ) )
        ++begin;
    while( end > begin && ( s[ end - 1 ] == ' ' || s[ end - 1 ] == '\t' || s[ end - 1 ] == '\r' ) )
        --end;
    return s.substr( begin, end - begin );
}

inline std::vector<std::string> SplitCommas( const std::string& line )
{
    std::vector<std::string> fields;
    size_t begin = 0;
    for( size_t i = 0; i <= line.size(); ++i )
    {
        if( i == line.size() || line[ i ] == ',' )
        {
            fields.push_back( line.substr( begin, i - begin ) );
            begin = i + 1;
        }
    }
    return fields;
}

// "ADDR (00)" and "GET_CONFIGURATION(21)" are both accepted -- the export puts
// a space before the parenthesis on some labels and not on others. Returns the
// name with trailing space removed, and the byte.
inline bool ParseLabel( const std::string& label, std::string* name, uint8_t* value )
{
    if( label.size() < 4 || label[ label.size() - 1 ] != ')' )
        return false;
    const size_t open = label.rfind( '(' );
    if( open == std::string::npos )
        return false;

    const std::string digits = label.substr( open + 1, label.size() - open - 2 );
    if( digits.size() != 2 )
        return false;
    for( char c : digits )
        if( !( ( c >= '0' && c <= '9' ) || ( c >= 'A' && c <= 'F' ) || ( c >= 'a' && c <= 'f' ) ) )
            return false;

    *name = Trim( label.substr( 0, open ) );
    *value = static_cast<uint8_t>( std::strtoul( digits.c_str(), nullptr, 16 ) );
    return !name->empty();
}

} // namespace detail

// Parse the dump into chip-select-delimited frames. Returns false and writes a
// message on anything it does not understand: a row silently skipped would
// shorten a transaction, and the decode of the shortened transaction could
// still come out looking sensible.
inline bool LoadCaptureDump( const std::string& path, std::vector<DumpFrame>* out, DumpStats* stats, std::string* error )
{
    std::ifstream in( path );
    if( !in )
    {
        *error = "cannot open " + path;
        return false;
    }

    out->clear();
    *stats = DumpStats{};

    const size_t kFieldCount = 10; // timestamp + 8 signals + label
    const size_t kChipSelectField = 8;
    const size_t kLabelField = 9;

    std::string line;
    int line_number = 0;

    // Header row. Checked rather than skipped, so a differently shaped export
    // is rejected here instead of being misread row by row.
    if( !std::getline( in, line ) )
    {
        *error = path + ": empty file";
        return false;
    }
    ++line_number;
    {
        const std::vector<std::string> fields = detail::SplitCommas( line );
        if( fields.empty() || detail::Trim( fields.front() ) != "Timestamp" || detail::Trim( fields.back() ) != "eSPI" )
        {
            *error = path + ":1: not the expected export header";
            return false;
        }
    }

    // An index rather than a pointer: push_back below reallocates, and a
    // pointer into the vector would be left dangling.
    const size_t kNoFrame = static_cast<size_t>( -1 );
    size_t open_frame = kNoFrame;
    bool turned_around = false;
    double previous_time = 0.0;
    bool have_previous_time = false;
    int last_data_line = 0;

    while( std::getline( in, line ) )
    {
        ++line_number;
        if( detail::Trim( line ).empty() )
            continue;

        const std::vector<std::string> fields = detail::SplitCommas( line );
        if( fields.size() != kFieldCount )
        {
            *error = path + ":" + std::to_string( line_number ) + ": expected " + std::to_string( kFieldCount )
                     + " fields, got " + std::to_string( fields.size() );
            return false;
        }

        double time = 0.0;
        if( !detail::ParseMilliseconds( fields[ 0 ], &time ) )
        {
            *error = path + ":" + std::to_string( line_number ) + ": '" + fields[ 0 ] + "' is not a timestamp";
            return false;
        }
        if( have_previous_time && time < previous_time )
        {
            *error = path + ":" + std::to_string( line_number ) + ": timestamp goes backwards";
            return false;
        }
        previous_time = time;
        have_previous_time = true;

        ++stats->rows;
        last_data_line = line_number;

        const std::string label = detail::Trim( fields[ kLabelField ] );
        const std::string chip_select = detail::Trim( fields[ kChipSelectField ] );

        if( label.empty() )
        {
            if( stats->unlabelled_row != 0 )
            {
                *error = path + ":" + std::to_string( line_number ) + ": a second row with no label";
                return false;
            }
            stats->unlabelled_row = line_number;
        }

        const bool idle = ( label == "IDLE" || label.empty() );
        if( chip_select != ( idle ? "1" : "0" ) )
            stats->chip_select_disagreements.push_back( line_number );

        if( idle )
        {
            if( !label.empty() )
                ++stats->idle_rows;
            open_frame = kNoFrame;
            turned_around = false;
            continue;
        }

        std::string name;
        uint8_t value = 0;
        if( !detail::ParseLabel( label, &name, &value ) )
        {
            *error = path + ":" + std::to_string( line_number ) + ": cannot read a byte out of '" + label + "'";
            return false;
        }

        if( name == "TURN" )
        {
            if( open_frame == kNoFrame )
            {
                *error = path + ":" + std::to_string( line_number ) + ": turn-around outside a chip select frame";
                return false;
            }
            if( turned_around )
            {
                *error = path + ":" + std::to_string( line_number ) + ": second turn-around in one frame";
                return false;
            }
            ++stats->turnaround_rows;
            ( *out )[ open_frame ].frame.has_turnaround = true;
            ( *out )[ open_frame ].last_line = line_number;
            turned_around = true;
            continue;
        }

        ++stats->byte_rows;
        if( open_frame == kNoFrame )
        {
            out->push_back( DumpFrame{} );
            open_frame = out->size() - 1;
            ( *out )[ open_frame ].first_line = line_number;
        }

        DumpFrame& frame = ( *out )[ open_frame ];
        frame.last_line = line_number;
        ( turned_around ? frame.frame.response : frame.frame.command ).push_back( value );
        frame.bytes.push_back( DumpByte{ name, value, line_number, turned_around } );
    }

    // The unlabelled row is only tolerated where it actually is -- at the end.
    // Anywhere else it would be a row whose meaning nobody has established,
    // sitting in the middle of a transaction and silently ending it.
    if( stats->unlabelled_row != 0 && stats->unlabelled_row != last_data_line )
    {
        *error = path + ":" + std::to_string( stats->unlabelled_row ) + ": row with no label is not the last row";
        return false;
    }

    stats->ends_mid_frame = ( open_frame != kNoFrame );
    return true;
}

// ---------------------------------------------------------------------------
//  The whole capture behind one ByteSource, so one LinkDecoder walks the file
//  from the first chip select assertion to the last, in order.
//
//  Per-frame accounting is delegated to FixtureByteSource rather than
//  reimplemented, so CommandFullyConsumed() and friends mean exactly what they
//  mean in T1.
//
//  NextTransaction() advances to the next chip select frame and is what the
//  test loop drives, because that is what a chip select edge does in L0: the
//  frame boundary is a physical event, not something the decoder decides. It
//  advances whether or not the previous frame was read to the end, which is
//  the resync contract -- one transaction the decoder stops early on must not
//  shift the next one.
//
//  HONEST LIMIT: LinkDecoder holds no state between transactions today (it
//  holds a ByteSource pointer and nothing else), so one decoder across 62
//  frames currently proves no more than 62 decoders would. This is the shape
//  the test should have for when session state -- I/O mode, CRC enable --
//  starts carrying across transactions, and it costs nothing now.
// ---------------------------------------------------------------------------
class CaptureByteSource : public espi::ByteSource
{
  public:
    explicit CaptureByteSource( const std::vector<DumpFrame>& frames ) : mFrames( frames ) {}

    // Move to the next chip select frame. False at the end of the capture.
    bool NextTransaction()
    {
        mIndex = mStarted ? mIndex + 1 : 0;
        mStarted = true;
        mCurrent.reset();
        if( mIndex >= mFrames.size() )
            return false;
        mCurrent.reset( new FixtureByteSource( mFrames[ mIndex ].frame ) );
        return true;
    }

    size_t Index() const { return mIndex; }
    const DumpFrame& Current() const { return mFrames[ mIndex ]; }

    // The per-frame accounting, for the phase-length comparison.
    const FixtureByteSource& Accounting() const { return *mCurrent; }

    bool ReadByte( espi::Phase phase, espi::StreamByte* out ) override
    {
        return mCurrent != nullptr && mCurrent->ReadByte( phase, out );
    }

    bool TurnAround( espi::ByteSpan* span ) override { return mCurrent != nullptr && mCurrent->TurnAround( span ); }

    bool Active() const override { return mCurrent != nullptr && mCurrent->Active(); }

    // The capture is Single I/O throughout: one byte occupies eight clocks on
    // every row of it, and no SET_CONFIGURATION to offset 008h ever appears.
    espi::IoMode Mode() const override { return espi::IoMode::Single; }

  private:
    const std::vector<DumpFrame>& mFrames;
    std::unique_ptr<FixtureByteSource> mCurrent;
    size_t mIndex = 0;
    bool mStarted = false;
};

} // namespace espi_test

#endif // ESPI_TEST_CAPTURE_DUMP_H
