#ifndef ESPI_ANALYZER_H
#define ESPI_ANALYZER_H

#include "EspiAnalyzerResults.h"
#include "EspiAnalyzerSettings.h"
#include "EspiSimulationGenerator.h"

#include <Analyzer.h>

#include <memory>

namespace espi
{
struct Field;
struct Transaction;
} // namespace espi

namespace espi_saleae
{

// The FrameV2 type name for a decoded field: its display name, lower-cased,
// with runs of anything that is not a letter or a digit collapsed to one
// underscore.
//
//     "Opcode"               -> opcode
//     "CRC"                  -> crc
//     "Virtual Wire Packet"  -> virtual_wire_packet
//     "PC_FREE"              -> pc_free
//
// docs/PLAN.md section 10 calls these names the contract downstream HLAs bind
// to, so deriving them from a display string is a real trade: one vocabulary
// instead of two and no table to keep in step, against a rename in the core
// quietly becoming a rename in the contract.
//
// Exposed rather than kept file-local because the fixtures that reach the
// emission path happen to have single-word field names, so a test driven only
// through WorkerThread cannot reach the separator at all -- deleting it left
// every test green. tests/test_sampling.cpp checks both: this function against
// the four cases above, and the emission path against the type names one
// fixture actually produces.
std::string FrameV2TypeName( const std::string& display_name );

// ---------------------------------------------------------------------------
//  The Logic 2 shell.
//
//  Inherits Analyzer2, not Analyzer -- docs/PLAN.md section 6, gotcha 9. The
//  bundled API documentation is out of date on this; the headers are not.
//
//  WorkerThread() does no protocol work of its own. It drives L0 to the next
//  CS# assertion, hands the resulting ByteSource to the core's LinkDecoder,
//  and turns the returned Field tree into frames. Every protocol decision --
//  how long a packet is, what a cycle type means, whether a CRC is right --
//  happens behind the R3 seam in src/core/, where a test can reach it without
//  a waveform.
// ---------------------------------------------------------------------------

class EspiAnalyzer : public Analyzer2
{
  public:
    EspiAnalyzer();
    ~EspiAnalyzer() override;

    void SetupResults() override;
    void WorkerThread() override;

    U32 GenerateSimulationData( U64 newest_sample_requested, U32 sample_rate,
                                SimulationChannelDescriptor** simulation_channels ) override;
    U32 GetMinimumSampleRateHz() override;
    const char* GetAnalyzerName() const override;
    bool NeedsRerun() override;

  private:
    // Walk one field, emitting frames for the byte-level fields underneath it.
    //
    // The tree mixes two kinds of child. Some are further bytes -- a virtual
    // wire packet's Count, Index and Data each have their own span. Others
    // merely explain the byte above them, and carry their parent's span
    // verbatim: every bit of a Status word, every named wire inside a Data
    // byte. Emitting both kinds as frames would produce overlapping frames,
    // which the SDK forbids (gotcha 7).
    //
    // So the test is the span, not the depth: recurse where a child covers
    // different samples from its parent, and stop where it does not, folding
    // those explanations into the frame's text instead.
    void EmitField( const espi::Field& field, U64* previous_end );

    std::unique_ptr<EspiAnalyzerSettings> mSettings;
    std::unique_ptr<EspiAnalyzerResults> mResults;

    // Demo mode. Built on the first request and kept afterwards: Logic 2 calls
    // GenerateSimulationData over and over, each time asking for a later
    // sample, and expects the same channels extended rather than new ones.
    EspiSimulationGenerator mSimulation;
};

} // namespace espi_saleae

extern "C" {
ANALYZER_EXPORT const char* __cdecl GetAnalyzerName();
ANALYZER_EXPORT Analyzer* __cdecl CreateAnalyzer();
ANALYZER_EXPORT void __cdecl DestroyAnalyzer( Analyzer* analyzer );
}

#endif // ESPI_ANALYZER_H
