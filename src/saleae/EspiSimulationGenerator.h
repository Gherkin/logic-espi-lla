#ifndef ESPI_SIMULATION_GENERATOR_H
#define ESPI_SIMULATION_GENERATOR_H

#include "EspiAnalyzerSettings.h"
#include "SimulationScript.h"

#include "espi/IoMode.h"
#include "espi/LaneCodec.h"

#include <SimulationChannelDescriptor.h>

#include <array>
#include <cstdint>

namespace espi_saleae
{

// ---------------------------------------------------------------------------
//  The simulation generator -- what Logic 2's demo mode shows.
//
//  It lays the literal bytes of SimulationScript() onto six simulated channels
//  as lane transitions, and knows nothing else. It cannot compute a packet
//  length, look up an opcode or reach src/core/tables/. That is what makes the
//  closed loop in tests/test_simulation.cpp evidence rather than a round trip:
//  the decoder recovers the phase split from the opcode and the shape table,
//  and nothing on this side of the loop helped it.
//
//  THE WAVEFORM MODEL, from the specification:
//
//    - CLK idles low and is low at the CS# assertion edge (section 3, p.21).
//    - Data is launched on the falling edge and sampled on the rising edge, so
//      each bit group is placed a half period before the rising edge that reads
//      it (section 3, p.21).
//    - The Turn-Around window is two clocks with the lines high -- driven for
//      the first, then tri-stated onto the weak pull-up (section 3.3, p.28).
//      Undriven lanes are modelled high throughout for the same reason, which
//      in Single I/O includes I/O[1] for the whole command phase and I/O[0]
//      for the whole response phase (Figure 56, p.88).
//
//  This is the same reading of the same pages that L0 was written from, so the
//  two agreeing is not independent evidence about what the page says --
//  tests/support/WaveformSerializer.h says the same of itself, and the answer
//  in both cases is that only a second human reader closes that gap. What the
//  closed loop does prove is that the waveform this generator ships in demo
//  mode is one this analyzer decodes.
//
//  THE CLOCK KEEPS TOGGLING BETWEEN TRANSACTIONS. A real bus stops it -- Figure
//  10 shows the clock idle outside CS# -- but a decoder that legitimately looks
//  one byte past the end of a malformed packet must find a transition there
//  rather than the end of the capture, which offline is an exception that
//  terminates the whole worker. L0 bounds every clock edge against the CS#
//  deassertion, so the extra toggles change no decode.
//
//  WHY IT IS NOT BUILT ON SimulationChannelDescriptorGroup. Its AdvanceAll() is
//  an empty stub in the offline harness (docs/PLAN.md section 6, gotcha 1), so
//  a generator written against the group API produces a correct waveform in
//  Logic 2 and a flat line in every test. Its Add() is worse: it returns a
//  pointer into a std::vector it keeps push_back-ing to, so an earlier pointer
//  dangles the moment the vector grows. This holds its own fixed array and
//  advances each descriptor by hand.
//
//  GetCurrentBitState() is never called: it is hard-coded to BIT_LOW offline
//  (gotcha 2). TransitionIfNeeded() tracks the state internally and is safe.
// ---------------------------------------------------------------------------

class EspiSimulationGenerator
{
  public:
    // Build the channels. The analyzer calls this on the first simulation
    // request, as the SDK's own samples do.
    void Initialize( U32 simulation_sample_rate, EspiAnalyzerSettings* settings );

    // True once there are channels to draw on. A settings object with no
    // channels assigned yet leaves this false and adds nothing, so calling
    // Initialize again later is safe and is what the analyzer does -- a first
    // simulation request can arrive before the dialog has been filled in.
    // Calling it again after it HAS channels would append a second set, so the
    // analyzer must keep asking this first.
    bool Ready() const { return mCount != 0; }

    // Extend the waveform until it reaches newest_sample_requested, then hand
    // back the channel array.
    //
    // THE ARRAY IS DESCRIPTORS, NOT POINTERS TO THEM: `*simulation_channels`
    // is set to the base of a contiguous array and the count is returned, which
    // is the convention SimulationChannelDescriptorGroup::GetArray() exists to
    // serve and the one Logic 2 reads. Writing simulation_channels[1] and
    // beyond would smash the caller's stack, because the caller passes the
    // address of a single pointer variable.
    //
    // The offline harness reads it the other way round -- AnalyzerTest::Instance::
    // RunSimulation() passes an array of sixteen pointers and memcpy()s `count`
    // of them back out -- so the two agree only for a single-channel analyzer.
    // tests/test_simulation.cpp therefore drives this class directly rather
    // than through RunSimulation(), which is also what the analyzer does.
    U32 GenerateSimulationData( U64 newest_sample_requested, U32 sample_rate,
                                SimulationChannelDescriptor** simulation_channels );

    // How many transactions have been laid down so far. Nothing in the plugin
    // reads it; T3 does, and it is the check that every transaction the
    // generator drew comes back out of the decoder. A waveform can lose its
    // last one to a dropped final transition without any comparison of decoded
    // text noticing -- there is simply one block fewer to compare.
    U64 TransactionsEmitted() const { return mEmitted; }

    // The script this run is actually replaying, which is not always the whole
    // of SimulationScript() -- the mode excursion is dropped when the run
    // cannot draw it. T3 needs the same list to know what to compare against.
    const std::vector<SimTransaction>& Script() const { return mScript; }

  private:
    // Our own AdvanceAll: every channel moves together, so one cursor is
    // enough. See the note above on why the SDK's is not usable.
    void AdvanceAll( U32 samples );
    void SetLevel( SimulationChannelDescriptor* channel, bool high );

    // One serial clock period: the lanes settle, then the rising edge that
    // samples them, then the falling edge.
    void EmitClock( const espi::LaneBits& lanes );
    void EmitByte( uint8_t value, espi::Phase phase );
    void EmitIdleClocks( U32 count );
    void EmitTransaction( const SimTransaction& transaction );

    EspiAnalyzerSettings* mSettings = nullptr;

    // The mode the transaction being laid down is in, set from the script for
    // each one. Seeded from the settings so Initialize can work out how many
    // lanes the run needs before any transaction has been emitted.
    espi::IoMode mMode = espi::IoMode::Single;
    std::vector<SimTransaction> mScript;
    U32 mSimulationRateHz = 0;
    U32 mHalfPeriod = 0;
    U64 mSample = 0;
    U64 mEmitted = 0;
    size_t mCursor = 0;

    // Fixed storage, filled compactly from the front: CS#, CLK, then whichever
    // lanes the settings assign. Never resized, never copied -- the pointers
    // below and the array Logic 2 receives both have to stay valid across every
    // later call.
    std::array<SimulationChannelDescriptor, 6> mChannels{};
    U32 mCount = 0;

    SimulationChannelDescriptor* mCs = nullptr;
    SimulationChannelDescriptor* mClk = nullptr;
    SimulationChannelDescriptor* mIo[ 4 ] = { nullptr, nullptr, nullptr, nullptr };
};

} // namespace espi_saleae

#endif // ESPI_SIMULATION_GENERATOR_H
