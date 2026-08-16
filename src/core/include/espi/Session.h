#ifndef ESPI_SESSION_H
#define ESPI_SESSION_H

#include "espi/ConfigRegisters.h"
#include "espi/Decode.h"
#include "espi/IoMode.h"

#include <string>

namespace espi
{

// The mode's name, spelled as §6.2.1.3 p.95 spells the I/O Mode Select
// encodings it decodes to. One copy, because both the decode tree and the
// per-transaction summary below have to say it and two copies drift.
const char* IoModeName( IoMode mode );

// One line saying what a transaction does to the session, for a presentation
// surface with a row per chip select rather than a row per field. Empty when
// the transaction changes nothing, which is nearly all of them.
//
// The decode tree already carries this as a Session block, but that block has
// no sample span -- the bytes behind it are drawn as the Data field or as
// Register Reset -- so it reaches no bubble and no tabular row. This is the
// same statement in the form a frame-level record can carry.
std::string DescribeSessionUpdate( const SessionUpdate& update );

// ---------------------------------------------------------------------------
//  L3 -- session state.
//
//  WHY THIS IS A LAYER AND NOT A SETTING. Nothing on an eSPI bus announces the
//  I/O mode. A byte is eight clocks, or four, or two, depending on a register
//  the controller wrote earlier, and the wire carries no marker to say which.
//  An analyzer that reads the mode from a settings dialog decodes a capture
//  correctly right up to the first SET_CONFIGURATION that changes it, and
//  produces confident nonsense from there on -- which is worse than failing,
//  because every field still has a name and a value.
//
//  So the mode is carried, not configured. This class is what carries it: one
//  decoded transaction at a time, in the order the chip selects happened.
//
//  THE EDGE. Both things that change the state land on the deassertion edge of
//  the chip select that carried them -- §5.1 p.86 for the I/O mode, §5.2 p.90
//  for CRC checking, §6.2 pp.92-93 for register writes in general, and §8.3.2
//  p.123 for the In-band RESET. That is why Apply() takes a whole transaction
//  and is called after it has been decoded: the transaction that causes the
//  change is itself decoded in the old mode, right down to its CRC.
//
//  WHAT IT DELIBERATELY DOES NOT DO. It never looks at bytes and never reads a
//  field by name -- LinkDecoder hands it a Transaction::session struct worked
//  out while the packet was in front of it. A state machine that scraped
//  rendered text would break silently the first time a field was reworded.
//
//  UNCERTAINTY IS A STATE, not an error. §8.3.2, p.122: "As the transaction
//  does not complete successfully, it is uncertain on the state of the
//  interface settings after the error." When that happens there is nothing
//  better to carry on with than the settings that were already in force, so
//  that is what this does -- and it says so, because a decode that continues on
//  a guess and does not admit it is the failure mode this whole class exists
//  to avoid. The specification's own remedy is the In-band RESET, which puts
//  the link back to a state both sides know.
// ---------------------------------------------------------------------------

class SessionState
{
  public:
    // A capture that begins at eSPI Reset#: Single I/O, CRC checking off, from
    // the Default column of §6.2.1.3.
    SessionState();

    // A capture that begins mid-session. Nothing on the wire says what mode
    // the bus was already in, so this is the one fact only the person who took
    // the capture can supply. CRC checking starts off regardless: it changes
    // no packet length -- "CRC generation is mandatory for eSPI" (§5.2, p.90)
    // -- so getting it wrong costs a verdict, not a decode.
    explicit SessionState( IoMode initial );

    // Fold in one decoded transaction, as of its chip select deassertion edge.
    void Apply( const Transaction& transaction );

    IoMode Mode() const { return mMode; }
    bool CrcChecking() const { return mCrcChecking; }

    // True once a write to General Capabilities and Configurations failed to
    // complete and has not been re-established since. Everything decoded while
    // this is set rests on settings the bus never confirmed.
    bool Uncertain() const { return mUncertain; }

    // How many times Apply() has actually changed the I/O mode.
    //
    // Nothing in the decode path reads it; the tests do. Comparing rendered
    // text cannot tell a decoder that followed a mode switch from one that was
    // handed the right mode to begin with -- both produce identical output --
    // so the count is the only way to assert the switch happened rather than
    // was never needed.
    unsigned ModeChanges() const { return mModeChanges; }

  private:
    void Adopt( const GeneralConfig& config );

    IoMode mMode = IoMode::Single;
    bool mCrcChecking = false;
    bool mUncertain = false;
    unsigned mModeChanges = 0;
};

} // namespace espi

#endif // ESPI_SESSION_H
