#ifndef ESPI_TEST_FIELD_INVARIANTS_H
#define ESPI_TEST_FIELD_INVARIANTS_H

// Structural properties every decode tree must have, checked on every fixture
// rather than asserted about one.
//
// SPAN CONTAINMENT: a field's span must cover every one of its descendants'.
//
// The spans exist so the shell can turn a field back into a Frame with the
// right start and end (see ByteStream.h). A container that claims fewer
// samples than the bytes it holds is therefore a field that would draw the
// wrong bubble -- and nothing else in the suite can see it, because Render()
// prints no spans and .expected files compare text.
//
// It is checked here rather than inside the decoder because it is a property
// of the whole tree, which no single construction site can see. `Virtual Wire
// Packet` was built with the count byte's span and then had Index and Data
// added past it, and stayed that way through six stages of QC and 170
// mutations without anything noticing.
//
// A field with no span of its own is skipped rather than failed: WAIT_STATE
// runs and error fields are deliberately built with a default ByteSpan,
// meaning "this never appeared on the wire as its own bytes". Their children,
// if any, are still walked.

#include "espi/Decode.h"

#include <cstdio>
#include <string>

namespace espi_test
{

// A default-constructed ByteSpan is {0, 0}. Nothing real is one sample long --
// the shortest thing on the wire is the two-clock turn-around -- so that is
// the test for "this field has no span".
inline bool HasSpan( const espi::Field& f )
{
    return f.span.last > f.span.first;
}

// Returns the number of violations found, reporting each.
inline size_t CheckSpanContainment( const espi::Field& field, const char* context, const char* path_prefix = "" )
{
    const std::string path = std::string( path_prefix ) + ( path_prefix[ 0 ] == '\0' ? "" : " > " ) + field.name;

    size_t violations = 0;
    for( const espi::Field& child : field.children )
    {
        if( HasSpan( field ) && HasSpan( child )
            && ( child.span.first < field.span.first || child.span.last > field.span.last ) )
        {
            std::fprintf( stderr,
                          "FAIL  %s: '%s' spans [%llu,%llu] but its child '%s' spans [%llu,%llu]\n", context,
                          path.c_str(), static_cast<unsigned long long>( field.span.first ),
                          static_cast<unsigned long long>( field.span.last ), child.name.c_str(),
                          static_cast<unsigned long long>( child.span.first ),
                          static_cast<unsigned long long>( child.span.last ) );
            ++violations;
        }
        violations += CheckSpanContainment( child, context, path.c_str() );
    }
    return violations;
}

inline size_t CheckSpanContainment( const espi::Transaction& txn, const char* context )
{
    size_t violations = 0;
    for( const espi::Field& f : txn.fields )
        violations += CheckSpanContainment( f, context );
    return violations;
}

} // namespace espi_test

#endif // ESPI_TEST_FIELD_INVARIANTS_H
