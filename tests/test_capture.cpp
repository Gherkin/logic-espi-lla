// T4 -- the capture, end to end, against the other decoder's own reading of it.
//
// tests/vectors/espi_dump.txt is 857 lines of an eSPI link coming up, exported
// by a third-party decoder from real hardware. It is the only fixture in this
// tree produced by an implementation unrelated to ours, and it does not merely
// contain bytes: it contains that decoder's field-by-field interpretation of
// them, one label per byte, 732 of them.
//
// THE WHOLE CAPTURE IS FED THROUGH ONE DECODER IN ORDER, and three separate
// comparisons are made on the way past:
//
//   1. PER-BYTE FIELD ATTRIBUTION.  For every one of the 732 byte rows, what
//      does the export call this byte, and what does our decoder attribute it
//      to? `kVocabulary` below is the hand-written correspondence between the
//      two vocabularies. This is the comparison with the most evidence behind
//      it and the one this test exists for.
//   2. PHASE LENGTHS.  The export states where each command phase ends, with
//      its TURN row. Our decoder works that out from the opcode and the packet
//      shape table, having never seen the file. A shape one byte long or short
//      fails here.
//   3. THE RENDERED DECODE, diffed against tests/vectors/espi_dump.expected.
//
// WHAT EACH IS WORTH. (1) and (2) are checked against somebody else's work:
// 720 byte attributions, 61 phase splits and 61 opcode names that nobody here
// wrote. (3) is checked against a file written from the same reading of the
// same specification pages as the decoder, so it catches drift and nothing
// else. Every fault found in the stage B, C and E reading gates decoded real
// traffic perfectly and would pass (3) today.
//
// AND WHAT NONE OF THEM IS WORTH. Two decoders agreeing is two readings
// agreeing, not a proof. Where they disagree -- IsKnownDisagreement below --
// ours is argued from a cited page, and the arguing is the point.
//
// WHAT THE CAPTURE CONTAINS. Configuration and virtual wire traffic only: 55
// GET_CONFIGURATION, 2 SET_CONFIGURATION, 4 GET_VWIRE, and one RESET that was
// already in progress when the export began. No wait states, no GET_STATUS, no
// PUT_VWIRE, nothing malformed, and no peripheral, OOB or flash packets. So T4
// reaches stages A through C and touches nothing in D or E.
//
// That RESET is the whole reason section 8.3.2 got read at all: it is frame 0,
// the export calls all twelve of its bytes Reserved, and nothing in Table 2
// points at the section that defines it. It is a fragment -- the chip select
// assertion edge is not in the file -- so it is decoded and flagged against
// Figure 65 rather than treated as evidence about a well formed RESET.
//
// RULE R2. tests/vectors/espi_dump.expected was written out longhand from the
// bytes and the specification -- never by running the decoder and keeping what
// it printed. The capture holds 62 frames but only 14 distinct transactions,
// so the file is 14 hand-written blocks pasted into capture order, one block
// per frame, mapped by the frame's literal bytes.

#include "espi/Decode.h"
#include "espi/LinkDecoder.h"
#include "support/CaptureDump.h"
#include "support/FixtureByteSource.h"
#include "support/TestMacros.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace espi;
using espi_test::CaptureByteSource;
using espi_test::DumpByte;
using espi_test::DumpFrame;
using espi_test::DumpStats;

namespace
{

std::string DumpPath()
{
    return std::string( ESPI_VECTOR_DIR ) + "/espi_dump.txt";
}

std::string ExpectedPath()
{
    return std::string( ESPI_VECTOR_DIR ) + "/espi_dump.expected";
}

std::string ReadFile( const std::string& path, bool* ok )
{
    std::ifstream in( path );
    if( !in )
    {
        *ok = false;
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    *ok = true;
    return ss.str();
}

bool LoadDump( std::vector<DumpFrame>* frames, DumpStats* stats )
{
    std::string error;
    if( espi_test::LoadCaptureDump( DumpPath(), frames, stats, &error ) )
        return true;
    std::fprintf( stderr, "FAIL  %s\n", error.c_str() );
    TEST_CHECK( false );
    return false;
}

// The frame the capture opens in the middle of. Chip select is already
// asserted on the export's first row, so this is a fragment of a frame that
// began before the capture did.
const size_t kFragmentFrame = 0;

// ---------------------------------------------------------------------------
//  THE TWO VOCABULARIES.
//
//  Left: every distinct byte label the export uses. Right: the field our
//  decoder must attribute that byte to. Typed from the two sides, not
//  generated from either.
//
//  Most rows are synonyms -- STS and Status are the same field under two
//  names, and a decoder is free to name its fields what it likes. Those carry
//  no note. A row where the two decoders genuinely say different things about
//  the byte carries a reason, and there is exactly one of those.
//
//  This table is total: a label not in it is a failure, not a skip. That is
//  what stops the comparison quietly shrinking to the labels we happened to
//  think of.
// ---------------------------------------------------------------------------
struct Vocabulary
{
    const char* theirs;
    const char* ours;
    const char* note; // nullptr where the two names mean the same thing
};

const Vocabulary kVocabulary[] = {
    { "GET_CONFIGURATION", "Opcode", nullptr },
    { "SET_CONFIGURATION", "Opcode", nullptr },
    { "GET_VWIRE", "Opcode", nullptr },
    { "ADDR", "Address", nullptr },
    { "Data", "Data", nullptr },
    { "CRC", "CRC", nullptr },
    { "RESP", "Response", nullptr },
    { "STS", "Status", nullptr },
    { "Index", "Index", nullptr },

    // The one substantive disagreement in the whole file, and it is a naming
    // one with teeth. The export calls the virtual wire count byte "Length".
    // Section 4.2.2, p.57: "The 6-bit count field allows up to 64 Virtual Wire
    // groups to be communicated in the same packet. This is a 0-based count."
    // Read as a length, 00h means no groups follow and the packet ends; read
    // as the 0-based count it is, 00h means one group follows and two more
    // bytes come after it. Every GET_VWIRE in this capture carries 00h, and
    // the export goes on to label the two bytes after it Index and Data -- so
    // its own framing agrees with ours and only the word is different.
    { "Length", "Count", "section 4.2.2 p.57 calls it the Virtual Wire Count, and it is 0-based" },

    // The 12 bytes of the opening fragment, handled by IsKnownDisagreement.
    // The export does not recognise FFh as an opcode; Table 2 p.27 and section
    // 8.3.2 pp.122-123 do, so this is the one place ours reads more than theirs.
    { "Reserved", nullptr, "the opening fragment -- see IsKnownDisagreement" },
};

const char* OurNameFor( const std::string& theirs, bool* known )
{
    for( const Vocabulary& v : kVocabulary )
    {
        if( theirs == v.theirs )
        {
            *known = true;
            return v.ours;
        }
    }
    *known = false;
    return nullptr;
}

// ---------------------------------------------------------------------------
//  WHERE THE TWO DECODERS DISAGREE, AND WHY OURS IS RIGHT.
//
//  One entry, covering the 12 bytes of frame 0.
//
//  The export labels all twelve "Reserved", i.e. it does not recognise FFh as
//  a command opcode at all. Table 2, p.27 defines FFh as RESET, "In-band RESET
//  command", and section 8.3.2 pp.122-123 gives the whole transaction with
//  Figure 65 drawing it. So the export is wrong about the first byte, and we
//  name it -- and wrong about the other eleven, which section 8.3.2 makes the
//  bits the target ignores rather than anything reserved.
//
//  THIS ENTRY USED TO SAY WE DECODED LESS OF THIS FRAME THAN THEY DID. Until
//  section 8.3.2 was read, RESET had no transcribed shape, so the decoder named
//  the opcode and attributed nothing to the eleven bytes behind it. That was a
//  gap in our transcription rather than in the document, and it is now closed.
//
//  WHAT REMAINS TRUE, AND IS WHY THIS FRAME IS STILL NOT EVIDENCE ABOUT WHAT A
//  RESET LOOKS LIKE. It is a fragment. Chip select was already asserted on the
//  export's pre-trigger row, so the assertion edge is not in the file at all
//  and the byte boundaries are wherever the other decoder started counting.
//  Twelve bytes is 96 clocks where Figure 65 draws 16, and only the first four
//  are FFh -- so our decode reports the frame and flags it against the figure,
//  which is all either side can honestly do with it. tests/vectors/link/reset.espi
//  is the well formed command, hand built, and says so.
// ---------------------------------------------------------------------------
bool IsKnownDisagreement( size_t frame, size_t byte_index, const std::string& theirs, const char* ours )
{
    if( frame != kFragmentFrame )
        return false;
    if( theirs != "Reserved" )
        return false;

    // Byte 0: they say Reserved, we say Opcode -- Table 2 p.27.
    if( byte_index == 0 )
        return ours != nullptr && std::string( ours ) == "Opcode";

    // Bytes 1-11: they say Reserved, we say Ignored -- section 8.3.2 p.122,
    // "Ignore all the subsequent bits received."
    return ours != nullptr && std::string( ours ) == "Ignored";
}

// ---------------------------------------------------------------------------
//  Turning our field tree into one name per byte.
//
//  FixtureByteSource lays byte i at samples [8i, 8i+7], so a span maps back to
//  a byte range by dividing by 8. Every field carrying a byte span and
//  covering the byte is a candidate; the narrowest wins, and on a tie the
//  shallowest does. Three properties of the tree decide the rest:
//
//   - A FIELD WHOSE CHILDREN SHARE ITS SPAN OWNS THE BYTE. Status spans two
//     bytes and its children are the individual bits, every one of them
//     carrying Status's own span. Narrowest-then-shallowest gives Status, not
//     PC_FREE, which is what the export means by STS.
//
//   - A FIELD WITH NO SPAN MUST NOT CLAIM BYTE 0. Error fields are built with
//     a default ByteSpan, which is {0, 0} and would otherwise look like it
//     covers the first byte. A real byte span is at least 8 samples, so
//     anything shorter is treated as no span at all. That also excludes TAR,
//     which is 2 samples -- deliberately, because the export gives the
//     turn-around its own row and that row is not a byte row.
//
//   - THE TOP LEVEL IS PHASE STRUCTURE, NOT A FIELD, so the search starts at
//     its children. `Command`, `TAR` and `Response` are the three entries in
//     Transaction::fields, and they exist to say which phase a byte is in.
//     The export never labels a byte "Command" -- it labels it ADDR or CRC --
//     so including them would let a container outrank the field that actually
//     names the byte whenever the two spans happen to be equal. They do become
//     equal: the opening fragment's Command holds one spanned child.
//
//     A byte inside a phase that no field inside that phase claims therefore
//     attributes to nothing, which is the right answer for the eleven bytes
//     after the fragment's RESET opcode.
//
//  Writing this traversal is what turned up the span defect that
//  espi_test::CheckSpanContainment now guards: three containers -- Virtual
//  Wire Packet, SMBus Packet and Message Code -- were each built with their
//  first byte's span and then had later bytes added, so each covered one byte
//  of the several it held. Field::Add now widens a parent to cover what is
//  added to it, and the invariant is checked on every fixture in
//  test_link.cpp. This traversal assumes the fixed behaviour.
// ---------------------------------------------------------------------------
const uint64_t kSamplesPerByte = 8;

bool HasByteSpan( const Field& f )
{
    return f.span.Valid() && ( f.span.last - f.span.first + 1 ) >= kSamplesPerByte;
}

bool Covers( const ByteSpan& span, size_t byte_index )
{
    return byte_index >= span.first / kSamplesPerByte && byte_index <= span.last / kSamplesPerByte;
}

struct Candidate
{
    const Field* field = nullptr;
    uint64_t width = 0;
    int depth = 0;
};

void CollectCandidates( const Field& f, int depth, size_t byte_index, Candidate* best )
{
    if( HasByteSpan( f ) && Covers( f.span, byte_index ) )
    {
        const uint64_t width = f.span.last - f.span.first + 1;
        if( best->field == nullptr || width < best->width || ( width == best->width && depth < best->depth ) )
            *best = Candidate{ &f, width, depth };
    }

    for( const Field& child : f.children )
        CollectCandidates( child, depth + 1, byte_index, best );
}

const Field* AttributeByte( const Transaction& txn, size_t byte_index )
{
    Candidate best;
    for( const Field& top : txn.fields )
        for( const Field& field : top.children )
            CollectCandidates( field, 0, byte_index, &best );
    return best.field;
}

// ---------------------------------------------------------------------------
//  The shape of the file itself.
//
//  Every count here was taken by hand off espi_dump.txt, and they exist
//  because the rest of this test is a comparison. A parser that dropped rows,
//  merged two chip select frames, or put the phase split in the wrong place
//  would produce a shorter capture that still compared cleanly against itself.
//  These numbers are the only thing standing between that and a green test.
// ---------------------------------------------------------------------------
void TestDumpStructure()
{
    std::vector<DumpFrame> frames;
    DumpStats stats;
    if( !LoadDump( &frames, &stats ) )
        return;

    // 856 data rows under the header, which is the whole file.
    TEST_CHECK_EQ( stats.rows, size_t( 856 ) );
    TEST_CHECK_EQ( stats.idle_rows, size_t( 62 ) );
    TEST_CHECK_EQ( stats.turnaround_rows, size_t( 61 ) );
    TEST_CHECK_EQ( stats.byte_rows, size_t( 732 ) );

    // ... and every row is one of those three kinds plus the unlabelled one.
    TEST_CHECK_EQ( stats.rows, stats.idle_rows + stats.turnaround_rows + stats.byte_rows + 1 );

    // The one row with no label, on line 857 -- the last one.
    TEST_CHECK_EQ( stats.unlabelled_row, 857 );
    TEST_CHECK( !stats.ends_mid_frame );

    TEST_CHECK_EQ( frames.size(), size_t( 62 ) );

    // Every byte in the file landed in a frame, and every one of them kept the
    // export's label. The bytes vector runs parallel to command ++ response.
    size_t bytes = 0;
    for( const DumpFrame& f : frames )
    {
        bytes += f.frame.command.size() + f.frame.response.size();
        TEST_CHECK_EQ( f.bytes.size(), f.frame.command.size() + f.frame.response.size() );
        for( size_t i = 0; i < f.bytes.size(); ++i )
        {
            const bool response = i >= f.frame.command.size();
            const uint8_t value =
                response ? f.frame.response[ i - f.frame.command.size() ] : f.frame.command[ i ];
            TEST_CHECK_EQ( f.bytes[ i ].value, value );
            TEST_CHECK( f.bytes[ i ].response == response );
            TEST_CHECK( !f.bytes[ i ].label.empty() );
        }
    }
    TEST_CHECK_EQ( bytes, stats.byte_rows );

    // Where the 732 comes from, so a reviewer can redo the arithmetic:
    //
    //   55 GET_CONFIGURATION   4 command + 8 response  = 660
    //    2 SET_CONFIGURATION   8 command + 4 response  =  24
    //    4 GET_VWIRE           2 command + 7 response  =  36
    //    1 fragment           12 command + 0 response  =  12
    //
    // The opcode counts are ours, read off the first command byte of each
    // frame. The export's own label column says 55 / 2 / 4 as well, and those
    // are two different implementations counting the same file.
    size_t by_opcode[ 4 ] = { 0, 0, 0, 0 };
    for( const DumpFrame& f : frames )
    {
        if( f.frame.command.empty() )
            continue;
        switch( f.frame.command[ 0 ] )
        {
        case 0x21:
            ++by_opcode[ 0 ];
            break;
        case 0x22:
            ++by_opcode[ 1 ];
            break;
        case 0x05:
            ++by_opcode[ 2 ];
            break;
        case 0xFF:
            ++by_opcode[ 3 ];
            break;
        default:
            std::fprintf( stderr, "FAIL  frame at line %d opens with unexpected opcode 0x%02X\n", f.first_line,
                          f.frame.command[ 0 ] );
            TEST_CHECK( false );
            break;
        }
    }
    TEST_CHECK_EQ( by_opcode[ 0 ], size_t( 55 ) );
    TEST_CHECK_EQ( by_opcode[ 1 ], size_t( 2 ) );
    TEST_CHECK_EQ( by_opcode[ 2 ], size_t( 4 ) );
    TEST_CHECK_EQ( by_opcode[ 3 ], size_t( 1 ) );
    TEST_CHECK_EQ( 55 * 12 + 2 * 12 + 4 * 9 + 12, int( stats.byte_rows ) );

    // The fragment has no turn-around; every real transaction has exactly one.
    TEST_CHECK( !frames[ kFragmentFrame ].frame.has_turnaround );
    TEST_CHECK( frames[ kFragmentFrame ].frame.response.empty() );
    TEST_CHECK_EQ( frames[ kFragmentFrame ].frame.command.size(), size_t( 12 ) );
    for( size_t i = 1; i < frames.size(); ++i )
    {
        if( !frames[ i ].frame.has_turnaround )
            std::fprintf( stderr, "FAIL  frame %zu (line %d) has no turn-around\n", i, frames[ i ].first_line );
        TEST_CHECK( frames[ i ].frame.has_turnaround );
        TEST_CHECK( !frames[ i ].frame.command.empty() );
        TEST_CHECK( !frames[ i ].frame.response.empty() );
    }

    // The chip select column agrees with the framing taken from the labels on
    // every row but one: line 2, the export's pre-trigger row at
    // -0.00012 ms, which is labelled IDLE with chip select already asserted.
    // That is the same fact the fragment frame rests on, arriving from the
    // other direction, and it is why this test does not simply trust the
    // column.
    TEST_CHECK_EQ( stats.chip_select_disagreements.size(), size_t( 1 ) );
    if( stats.chip_select_disagreements.size() == 1 )
        TEST_CHECK_EQ( stats.chip_select_disagreements[ 0 ], 2 );

    // Line numbers run forward and never overlap, so a failure below can be
    // read straight back into the file.
    for( size_t i = 0; i < frames.size(); ++i )
    {
        TEST_CHECK( frames[ i ].first_line <= frames[ i ].last_line );
        if( i > 0 )
            TEST_CHECK( frames[ i - 1 ].last_line < frames[ i ].first_line );
    }

    // Every label the export uses is one this test has an opinion about. A new
    // export carrying a label nobody mapped must fail here rather than be
    // silently compared against nothing.
    for( const DumpFrame& f : frames )
    {
        for( const DumpByte& b : f.bytes )
        {
            bool known = false;
            OurNameFor( b.label, &known );
            if( !known )
                std::fprintf( stderr, "FAIL  %s:%d: label '%s' is not in kVocabulary\n", "espi_dump.txt", b.line,
                              b.label.c_str() );
            TEST_CHECK( known );
        }
    }
}

// ---------------------------------------------------------------------------
//  The single chronological pass.
// ---------------------------------------------------------------------------
void TestCaptureAgainstExport()
{
    std::vector<DumpFrame> frames;
    DumpStats stats;
    if( !LoadDump( &frames, &stats ) )
        return;

    CaptureByteSource source( frames );
    LinkDecoder decoder( &source );

    std::string got;
    size_t decoded = 0;
    size_t clean = 0;
    size_t bytes_compared = 0;
    size_t bytes_agreed = 0;
    size_t disagreements_explained = 0;
    size_t opcode_names_checked = 0;
    size_t reported = 0;
    const size_t kMaxReported = 20;

    while( source.NextTransaction() )
    {
        const size_t i = source.Index();
        const DumpFrame& frame = source.Current();

        Transaction txn;
        if( !decoder.Decode( &txn ) )
        {
            std::fprintf( stderr, "FAIL  frame %zu (line %d) produced no decode\n", i, frame.first_line );
            TEST_CHECK( false );
            continue;
        }
        ++decoded;

        if( i != 0 )
            got += "---\n";
        got += Render( txn );

        // --- 1. per-byte field attribution ---------------------------------
        for( size_t b = 0; b < frame.bytes.size(); ++b )
        {
            const DumpByte& theirs = frame.bytes[ b ];
            const Field* ours_field = AttributeByte( txn, b );
            const char* ours = ( ours_field != nullptr ) ? ours_field->name.c_str() : nullptr;

            bool known = false;
            const char* expected = OurNameFor( theirs.label, &known );

            ++bytes_compared;

            if( known && expected != nullptr && ours != nullptr && std::string( ours ) == expected )
            {
                ++bytes_agreed;

                // Where the export's label is the opcode's own name, it is
                // also an independent statement of what that opcode is. Our
                // field text carries our name for it; they must match. Counted
                // as well as checked, because a rename on either side could
                // stop this branch firing without anything else noticing.
                if( std::string( expected ) == "Opcode" )
                {
                    ++opcode_names_checked;
                    if( ours_field->text.find( theirs.label ) == std::string::npos )
                    {
                        std::fprintf( stderr, "FAIL  espi_dump.txt:%d: export calls opcode 0x%02X '%s', we render '%s'\n",
                                      theirs.line, theirs.value, theirs.label.c_str(), ours_field->text.c_str() );
                        TEST_CHECK( false );
                    }
                }
                continue;
            }

            if( IsKnownDisagreement( i, b, theirs.label, ours ) )
            {
                ++disagreements_explained;
                continue;
            }

            if( reported < kMaxReported )
            {
                std::fprintf( stderr, "FAIL  espi_dump.txt:%d: export says '%s', we attribute the byte to '%s'\n",
                              theirs.line, theirs.label.c_str(), ours != nullptr ? ours : "(nothing)" );
                ++reported;
            }
            TEST_CHECK( false );
        }

        // --- 2. phase lengths ----------------------------------------------
        const espi_test::FixtureByteSource& accounting = source.Accounting();
        if( i == kFragmentFrame )
        {
            // The one frame with no turn-around: section 8.3.2, p.122, gives
            // RESET no response phase, so the generic assertions below do not
            // apply to it. What does apply is that the frame is fully
            // accounted for -- the decoder reads to the chip select edge
            // because p.123 has the target "Wait until CS# deassertion", so
            // every one of the twelve bytes is claimed by a field.
            //
            // No error: the frame does not match Figure 65 and says so, but a
            // controller driving the ignored bits low is a deviation the target
            // absorbs, not a decode that failed.
            if( !accounting.CommandFullyConsumed() )
                std::fprintf( stderr, "FAIL  frame %zu (line %d): %zu command byte(s) left unread\n", i,
                              frame.first_line, accounting.CommandBytesLeft() );
            TEST_CHECK( accounting.CommandFullyConsumed() );
            TEST_CHECK( accounting.ResponseFullyConsumed() );
            TEST_CHECK( !accounting.TurnedAround() );
            TEST_CHECK( !txn.HasError() );
            ++clean;
            continue;
        }

        if( !accounting.CommandFullyConsumed() )
            std::fprintf( stderr, "FAIL  frame %zu (line %d): %zu command byte(s) left unread\n", i, frame.first_line,
                          accounting.CommandBytesLeft() );
        TEST_CHECK( accounting.CommandFullyConsumed() );

        if( !accounting.ResponseFullyConsumed() )
            std::fprintf( stderr, "FAIL  frame %zu (line %d): %zu response byte(s) left unread\n", i, frame.first_line,
                          accounting.ResponseBytesLeft() );
        TEST_CHECK( accounting.ResponseFullyConsumed() );

        TEST_CHECK( accounting.TurnedAround() );

        if( txn.HasError() )
            std::fprintf( stderr, "FAIL  frame %zu (line %d) decoded with an error\n", i, frame.first_line );
        TEST_CHECK( !txn.HasError() );
        ++clean;
    }

    // Stated as numbers so that frames or bytes silently dropped from the loop
    // are visible rather than showing up as a shorter clean run.
    TEST_CHECK_EQ( decoded, size_t( 62 ) );
    TEST_CHECK_EQ( clean, size_t( 62 ) );
    TEST_CHECK_EQ( bytes_compared, size_t( 732 ) );
    TEST_CHECK_EQ( bytes_agreed, size_t( 720 ) );
    TEST_CHECK_EQ( disagreements_explained, size_t( 12 ) );

    // 55 GET_CONFIGURATION + 2 SET_CONFIGURATION + 4 GET_VWIRE. The opening
    // fragment's FFh is not among them: the export does not name it, so it
    // goes through IsKnownDisagreement instead.
    TEST_CHECK_EQ( opcode_names_checked, size_t( 61 ) );

    // --- 3. the rendered decode ---------------------------------------------
    bool ok = false;
    const std::string want = ReadFile( ExpectedPath(), &ok );
    if( !ok )
    {
        std::fprintf( stderr, "FAIL  cannot read %s\n", ExpectedPath().c_str() );
        TEST_CHECK( false );
        return;
    }

    if( got != want )
    {
        // 1225 lines is too much to print whole, so report the first line that
        // differs and the frame it belongs to.
        size_t line = 1;
        size_t at = 0;
        while( at < got.size() && at < want.size() && got[ at ] == want[ at ] )
        {
            if( got[ at ] == '\n' )
                ++line;
            ++at;
        }

        // Back up to the start of the line so the two are printed whole.
        size_t begin = at;
        while( begin > 0 && got[ begin - 1 ] != '\n' )
            --begin;

        std::fprintf( stderr, "FAIL  capture decode differs from espi_dump.expected at line %zu\n", line );
        std::fprintf( stderr, "        expected: %s\n", want.substr( begin, want.find( '\n', begin ) - begin ).c_str() );
        std::fprintf( stderr, "        got:      %s\n", got.substr( begin, got.find( '\n', begin ) - begin ).c_str() );
    }
    TEST_CHECK( got == want );
}

// ---------------------------------------------------------------------------
//  The parser has to reject what it cannot read.
//
//  Same reasoning as TestFixtureLoaderRejectsGarbage in test_link.cpp: a
//  loader that skipped rows it did not understand would shorten the capture,
//  and a shortened capture full of repeated polls still decodes cleanly.
// ---------------------------------------------------------------------------
void TestDumpParserRejectsGarbage()
{
    std::vector<DumpFrame> frames;
    DumpStats stats;
    std::string error;

    TEST_CHECK( !espi_test::LoadCaptureDump( std::string( ESPI_VECTOR_DIR ) + "/no_such_dump.txt", &frames, &stats, &error ) );
    TEST_CHECK( !error.empty() );
}

} // namespace

int main()
{
    TestDumpStructure();
    TestCaptureAgainstExport();
    TestDumpParserRejectsGarbage();
    TEST_MAIN_RETURN();
}
