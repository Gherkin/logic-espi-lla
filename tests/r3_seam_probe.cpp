// Rule R3 enforcement probe -- THIS FILE MUST NOT COMPILE.
//
// It is built by the `r3_seam` test, which is marked WILL_FAIL. If this ever
// compiles, the seam that keeps specification-derived mapping tables out of
// reach of test and encode code has been broken, and round-trip tests can
// start silently agreeing with themselves.
//
// The include below is exactly how src/core/src/Opcodes.cpp reaches the table.
// It resolves fine from inside espi_core's own sources, and must not resolve
// from anywhere else.
//
// Point this at a header that REALLY EXISTS under src/core/tables/. A probe
// naming a deleted file also fails to compile, and would pass this test while
// proving nothing at all.

#include "Opcodes.h"

int main()
{
    return 0;
}
