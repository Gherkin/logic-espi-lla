#ifndef ESPI_TEST_FIXTURE_BYTE_SOURCE_H
#define ESPI_TEST_FIXTURE_BYTE_SOURCE_H

// A ByteSource that serves literal bytes from a fixture file (rule R1).
//
// IT HAS NO PROTOCOL KNOWLEDGE, AND THAT IS THE POINT. It knows only three
// things: these bytes are the command phase, there is a turn-around here,
// those bytes are the response phase. It cannot compute a packet length, look
// up an opcode, or reach src/core/tables/ -- the include path does not exist
// from this directory (rule R3).
//
// So the fixture states where the command ends and the decoder works it out
// independently from the opcode and the shape table. If the decoder reads one
// byte too many, ReadByte runs out and the transaction comes back truncated.
// If it reads one too few, FullyConsumed() is false and the test fails. The
// phase split is a check on the decoder's length arithmetic, never a hint to
// it.
//
// FIXTURE FORMAT -- hex bytes, one transaction per CMD/TAR/RSP group:
//
//     # comment
//     CMD 21 00 20 C8
//     TAR
//     RSP 08 00 0B 00 00 0F 01 91
//
// Sample spans are synthetic: byte i occupies samples [i*8, i*8+7]. The core
// only carries spans through, so a plausible monotonic sequence is enough to
// check they are merged rather than dropped.

#include "espi/ByteStream.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace espi_test
{

struct Frame
{
    std::vector<uint8_t> command;
    std::vector<uint8_t> response;
    bool has_turnaround = false;
    std::string label;
};

// Parse a fixture file into its transactions. Returns false and writes a
// message if the file cannot be read or a line is not understood -- a silently
// skipped fixture is a test that asserts nothing.
inline bool LoadFixture( const std::string& path, std::vector<Frame>* out, std::string* error )
{
    std::ifstream in( path );
    if( !in )
    {
        *error = "cannot open " + path;
        return false;
    }

    out->clear();
    std::string line;
    int line_number = 0;
    while( std::getline( in, line ) )
    {
        ++line_number;
        std::istringstream ls( line );
        std::string keyword;
        if( !( ls >> keyword ) || keyword.empty() || keyword[ 0 ] == '#' )
            continue;

        std::vector<uint8_t>* target = nullptr;
        if( keyword == "CMD" )
        {
            out->push_back( Frame{} );
            target = &out->back().command;
        }
        else if( keyword == "RSP" )
        {
            if( out->empty() )
            {
                *error = path + ":" + std::to_string( line_number ) + ": RSP before any CMD";
                return false;
            }
            target = &out->back().response;
        }
        else if( keyword == "TAR" )
        {
            if( out->empty() )
            {
                *error = path + ":" + std::to_string( line_number ) + ": TAR before any CMD";
                return false;
            }
            out->back().has_turnaround = true;
            continue;
        }
        else
        {
            *error = path + ":" + std::to_string( line_number ) + ": unknown keyword '" + keyword + "'";
            return false;
        }

        std::string token;
        while( ls >> token )
        {
            if( token[ 0 ] == '#' )
                break;
            char* end = nullptr;
            const unsigned long value = std::strtoul( token.c_str(), &end, 16 );
            if( end == token.c_str() || *end != '\0' || value > 0xFF )
            {
                *error = path + ":" + std::to_string( line_number ) + ": '" + token + "' is not a hex byte";
                return false;
            }
            target->push_back( static_cast<uint8_t>( value ) );
        }
    }
    return true;
}

class FixtureByteSource : public espi::ByteSource
{
  public:
    explicit FixtureByteSource( const Frame& frame, espi::IoMode mode = espi::IoMode::Single )
        : mFrame( frame ), mMode( mode )
    {
    }

    bool ReadByte( espi::Phase phase, espi::StreamByte* out ) override
    {
        const std::vector<uint8_t>& bytes = ( phase == espi::Phase::Command ) ? mFrame.command : mFrame.response;
        size_t& pos = ( phase == espi::Phase::Command ) ? mCommandPos : mResponsePos;

        if( pos >= bytes.size() )
            return false;

        const size_t absolute = ( phase == espi::Phase::Command ) ? pos : mFrame.command.size() + pos;
        out->value = bytes[ pos ];
        out->span = espi::ByteSpan{ absolute * 8, absolute * 8 + 7 };
        ++pos;
        return true;
    }

    bool TurnAround( espi::ByteSpan* span ) override
    {
        if( !mFrame.has_turnaround )
            return false;
        mTurnedAround = true;
        const size_t absolute = mFrame.command.size() * 8;
        if( span != nullptr )
            *span = espi::ByteSpan{ absolute, absolute + 1 };
        return true;
    }

    bool Active() const override { return mCommandPos < mFrame.command.size() || mResponsePos < mFrame.response.size(); }

    espi::IoMode Mode() const override { return mMode; }

    // --- checks the test makes, not part of the ByteSource contract ---

    bool CommandFullyConsumed() const { return mCommandPos == mFrame.command.size(); }
    bool ResponseFullyConsumed() const { return mResponsePos == mFrame.response.size(); }
    size_t CommandBytesLeft() const { return mFrame.command.size() - mCommandPos; }
    size_t ResponseBytesLeft() const { return mFrame.response.size() - mResponsePos; }
    bool TurnedAround() const { return mTurnedAround; }

  private:
    const Frame& mFrame;
    espi::IoMode mMode;
    size_t mCommandPos = 0;
    size_t mResponsePos = 0;
    bool mTurnedAround = false;
};

} // namespace espi_test

#endif // ESPI_TEST_FIXTURE_BYTE_SOURCE_H
