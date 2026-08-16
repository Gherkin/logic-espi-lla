#ifndef ESPI_SIMULATION_SCRIPT_H
#define ESPI_SIMULATION_SCRIPT_H

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
//  WHERE THE BYTES CAME FROM. All eight transactions are real traffic: they are
//  transcribed from tests/vectors/espi_dump.txt, a third-party decoder's export
//  of an eSPI link coming up on real hardware, and they appear here in the order
//  the capture has them. Nothing here was constructed to make a decode look
//  good.
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

    // Provenance, checked by T3. `index` counts transactions within the
    // fixture file, from zero.
    const char* fixture = nullptr;
    int index = 0;

    // One line for whoever is reading the waveform in Logic 2.
    const char* summary = nullptr;
};

// The whole script, in capture order. The generator loops it for as long as
// Logic 2 asks for more samples.
const std::vector<SimTransaction>& SimulationScript();

} // namespace espi_saleae

#endif // ESPI_SIMULATION_SCRIPT_H
