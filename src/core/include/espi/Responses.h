#ifndef ESPI_RESPONSES_H
#define ESPI_RESPONSES_H

#include <cstdint>

namespace espi
{

// The response code carried in bits [3:0] of the response byte, plus the
// whole-byte NO_RESPONSE encoding.
//
// Transcribed in src/core/tables/Responses.h from base spec Table 3, p.30 --
// that header is where the values live and what the QC worksheet checks.
enum class ResponseCode : uint8_t
{
    Accept,
    Defer,
    NonFatalError,
    FatalError,
    WaitState,
    NoResponse,
};

struct ResponseInfo
{
    const char* name = nullptr;
    ResponseCode code = ResponseCode::Accept;
    uint8_t encoding = 0;  // the byte as seen on the bus
    uint8_t modifier = 0;  // bits [7:6], R1R0
    uint8_t reserved = 0;  // bits [5:4], which the spec requires to be 0
};

// Decode a response byte. Returns false for an encoding Table 3 does not
// define, which the decoder reports rather than guessing at.
//
// NO_RESPONSE (FFh) is matched as a whole byte before the low nibble is
// considered -- it shares the nibble 1111 with WAIT_STATE.
bool LookupResponse( uint8_t byte, ResponseInfo* out );

// True when the byte is a WAIT_STATE and not a NO_RESPONSE. Wait states are
// excluded from the response CRC (section 3.10, p.45), so the decoder must
// recognise them before accumulating.
bool IsWaitState( uint8_t byte );

// What R1R0 says is appended to a GET_STATUS ACCEPT response. Meaningful only
// in that one case; Table 3 Note 1 requires 00 everywhere else.
const char* ResponseModifierName( uint8_t r1r0 );

} // namespace espi

#endif // ESPI_RESPONSES_H
