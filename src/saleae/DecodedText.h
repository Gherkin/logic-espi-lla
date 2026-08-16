#ifndef ESPI_DECODED_TEXT_H
#define ESPI_DECODED_TEXT_H

#include <cstddef>
#include <string>

namespace espi_saleae
{

// ---------------------------------------------------------------------------
//  Reading the core's one decoded line back apart.
//
//  espi::Field::text is formatted as "<value>  <meaning>" -- two spaces, either
//  half may be absent (see Decode.h):
//
//      "0x08  ACCEPT"      value 0x08,        meaning ACCEPT
//      "0x88"              value 0x88,        no meaning
//      "2 clocks"          value "2 clocks",  no meaning
//      "bit 3 = 1  high"   value "bit 3 = 1", meaning high
//
//  Presentation needs the halves apart. A bubble with room for one word should
//  show "ACCEPT" and not "0x08", and a compact summary of a field's children
//  wants what each one resolved to rather than how it was spelled.
//
//  Parsing a display string is normally a bad idea. It is defensible here
//  because that format is a contract, not an implementation detail: Decode.h
//  calls it stable and every .expected file in the suite is written against it,
//  so a change to it fails hundreds of comparisons long before it reaches a
//  bubble. The alternative -- carrying the halves separately through every one
//  of the core's field construction sites -- buys nothing the format does not
//  already guarantee.
// ---------------------------------------------------------------------------

inline std::string MeaningOf( const std::string& text )
{
    const size_t at = text.find( "  " );
    if( at == std::string::npos )
        return {};
    return text.substr( at + 2 );
}

inline std::string ValueOf( const std::string& text )
{
    const size_t at = text.find( "  " );
    return at == std::string::npos ? text : text.substr( 0, at );
}

} // namespace espi_saleae

#endif // ESPI_DECODED_TEXT_H
