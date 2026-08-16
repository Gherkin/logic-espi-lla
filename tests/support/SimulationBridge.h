#ifndef ESPI_TEST_SIMULATION_BRIDGE_H
#define ESPI_TEST_SIMULATION_BRIDGE_H

// Turns what the simulation generator produced into what the analyzer reads.
//
// The offline harness stops one step short of the closed loop: SimulatedChannel
// records the transitions a generator laid down, MockChannelData serves the
// transitions an analyzer walks, and nothing connects them. This is the join.
//
// TWO HARNESS FAULTS SIT IN THESE FIFTEEN LINES, and both are silent.
//
// 1. THE SNIPPET IN docs/PLAN.md SECTION 3 IS WRONG. It collects absolute
//    sample numbers from GetCurrentSample() and passes them to
//    TestAppendTransitions, which treats its argument as RELATIVE -- it
//    forwards each one to TestAppendTransitionAfterSamples. The waveform comes
//    out stretched by roughly its own running sum, and nothing complains: it is
//    still a monotonic list of transitions, just not the one the generator
//    drew. TestAppendTransitionAtSamples is the absolute form, and
//    MakeChannelData below is the only place in this tree that calls it.
//
// 2. SimulatedChannel::AdvanceToNextTransition() NEVER YIELDS THE LAST
//    TRANSITION. Its guard is `mTransitionIndex >= mTransitions.size() - 1`,
//    so a channel with N transitions reports N-1 of them. On CS# that final
//    edge is the deassertion that closes the last transaction, so a bridge
//    that trusts the loop drops the end of the waveform and the last decode
//    comes back truncated.
//
//    The dropped transition is still reachable: after the loop stops, the
//    channel's cursor sits one short, so GetDurationToNextTransition() measures
//    exactly the gap to the transition it refused to hand over. Dividing by
//    GetSampleDuration() converts it back, and both come from the same sample
//    rate, so the round trip is exact to within the rounding llround absorbs.
//
//    On an empty channel -- one the generator never drove, which in Single I/O
//    is I/O[2] and I/O[3] -- the same guard underflows to SIZE_MAX and the
//    function walks into at(0) on an empty vector instead. That throws, and it
//    is caught here rather than prevented, because preventing it would mean
//    inventing an edge the generator never drew.
//
// Neither fault affects Logic 2. Both are the offline harness's, and the whole
// reason this file has a long comment and fifteen lines of code.

#include "support/WaveformSerializer.h"

#include <MockChannelData.h>
#include <MockSimulatedChannelDescriptor.h>
#include <SimulationChannelDescriptor.h>
#include <TestInstance.h>

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace espi_test
{

// Every transition the generator recorded on one simulated channel, absolute.
inline std::vector<uint64_t> SimulatedTransitions( AnalyzerTest::SimulatedChannel* channel, BitState* initial )
{
    channel->ResetToStart();
    if( initial != nullptr )
        *initial = channel->GetCurrentState();

    std::vector<uint64_t> transitions;
    try
    {
        while( channel->AdvanceToNextTransition() )
            transitions.push_back( channel->GetCurrentSample() );

        // The one the loop above refuses to report. See fault 2.
        const double duration = channel->GetDurationToNextTransition();
        const uint64_t last =
            channel->GetCurrentSample() + static_cast<uint64_t>( std::llround( duration / channel->GetSampleDuration() ) );
        if( transitions.empty() || last > transitions.back() )
            transitions.push_back( last );
    }
    catch( const std::out_of_range& )
    {
        // A channel with no transitions at all. See fault 2.
    }
    return transitions;
}

// The simulated channel, as an analyzer would read it.
inline AnalyzerTest::MockChannelData* BridgeSimulatedChannel( AnalyzerTest::Instance* instance,
                                                              SimulationChannelDescriptor* descriptor )
{
    AnalyzerTest::SimulatedChannel* channel =
        AnalyzerTest::SimulatedChannel::FromSimulatedChannelDescriptor( descriptor );

    BitState initial = BIT_LOW;
    const std::vector<uint64_t> transitions = SimulatedTransitions( channel, &initial );
    return MakeChannelData( instance, initial, transitions );
}

} // namespace espi_test

#endif // ESPI_TEST_SIMULATION_BRIDGE_H
