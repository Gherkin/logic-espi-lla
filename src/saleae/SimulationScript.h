#ifndef ESPI_SIMULATION_SCRIPT_H
#define ESPI_SIMULATION_SCRIPT_H

#include "espi/IoMode.h"

#include <cstdint>
#include <vector>

namespace espi_saleae
{

// ---------------------------------------------------------------------------
//  What demo mode replays.
//
//  THE SCRIPT IS LITERAL BYTES, and it has to be. The generator that lays it
//  down is a serializer, not an eSPI encoder -- the same discipline rule R1
//  puts on the fixtures (docs/PLAN.md section 4). If the simulation computed a
//  packet from the opcode tables, then decoded it with the same tables, demo
//  mode would agree with itself no matter what the tables said and T3 would
//  assert nothing at all.
//
//  So this file states where each transaction ends, byte for byte, and nothing
//  in src/saleae/ ever asks why. The decoder works the lengths out from the
//  opcode and the shape table behind the R3 seam, exactly as it does on a real
//  capture.
//
//  WHERE THE BYTES CAME FROM. The first eight transactions are real traffic:
//  they are transcribed from tests/vectors/espi_dump.txt, a third-party
//  decoder's export of an eSPI link coming up on real hardware, and they appear
//  here in the order the capture has them. Nothing there was constructed to
//  make a decode look good.
//
//  THE LAST THREE ARE NOT, and say so. The capture never leaves Single I/O --
//  it only ever addresses 0020h and 0030h, so the register that decides how
//  the bus is read is never written in it -- and a demo that never changes
//  mode cannot show the one thing phase 7 added. Those three are hand built
//  from the specification, like the fixtures they cite.
//
//  THE MODE IS STATED PER TRANSACTION, NOT WORKED OUT. This is the same
//  discipline as the bytes and it matters more here: a generator that decoded
//  the SET_CONFIGURATION it was laying down and switched itself would be
//  running the decoder's own reasoning on the other side of the loop, and the
//  two agreeing would assert nothing. It is told; the analyzer is not.
//
//  The `fixture` and `index` fields say which tests/vectors/link fixture holds
//  the same transaction. That is provenance for a human, and tests/test_simulation.cpp
//  turns it into a check: the script must equal the fixture byte for byte, and
//  the decode of the simulated waveform must equal the .expected file a human
//  wrote out longhand for it (rule R2). Neither file is generated from the
//  other, so drift in either direction is a failure rather than a silent
//  divergence between what demo mode shows and what the suite believes.
//
//  Nothing in the shell reads a file at run time. The plugin has no access to
//  tests/vectors when it is loaded into Logic 2.
// ---------------------------------------------------------------------------

struct SimTransaction
{
    std::vector<uint8_t> command;
    bool turnaround = false;
    std::vector<uint8_t> response;

    // The I/O mode this transaction goes out in. See the note above on why it
    // is stated rather than derived.
    espi::IoMode mode = espi::IoMode::Single;

    // Provenance, checked by T3. `index` counts transactions within the
    // fixture file, from zero.
    const char* fixture = nullptr;
    int index = 0;

    // One line for whoever is reading the waveform in Logic 2.
    const char* summary = nullptr;
};

// The whole script, in capture order, mode excursion included.
const std::vector<SimTransaction>& SimulationScript();

// The script as it can actually be replayed by a run that starts in
// `starting_mode` with `lanes_assigned` I/O channels wired up. The generator
// loops whatever this returns for as long as Logic 2 asks for more samples.
//
// TWO THINGS THE EXCURSION NEEDS, and it is dropped when either is missing:
//
//   - all four lanes, because it goes to Quad I/O;
//   - a Single I/O start, because it comes back through an In-band RESET,
//     which returns the link to Single whatever it was in (§8.3.2, p.123). The
//     script loops, so the mode it ends in has to be the mode it begins in, and
//     the RESET only makes those the same if that mode is Single.
//
// What is left over is the eight capture transactions, which carry no RESET
// and no configuration write and so decode identically at any mode -- which is
// what the demo did before phase 7, and what T3 still replays at Dual, at Quad
// and on a two-lane bus.
std::vector<SimTransaction> SimulationScriptFrom( espi::IoMode starting_mode, int lanes_assigned );

} // namespace espi_saleae

#endif // ESPI_SIMULATION_SCRIPT_H
