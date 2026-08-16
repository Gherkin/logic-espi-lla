#include "espi/Session.h"

namespace espi
{

SessionState::SessionState()
{
    GeneralConfig reset;
    if( GeneralConfigResetState( &reset ) )
    {
        mMode = reset.mode;
        mCrcChecking = reset.crc_checking;
    }
    // A false return means the field layout no longer names those two fields,
    // which is a broken transcription rather than anything on the bus. The
    // member initialisers already hold the same values; leaving them is the
    // one behaviour that cannot make a capture undecodable.
}

SessionState::SessionState( IoMode initial ) : mMode( initial )
{
}

void SessionState::Adopt( const GeneralConfig& config )
{
    // The two fields are independent, so a reserved mode selection does not
    // void the CRC Checking Enable bit written beside it.
    mCrcChecking = config.crc_checking;

    // p.95 gives encoding 11b no mode at all. A controller that wrote it has
    // said nothing about what it is going to talk in, so staying put is the
    // only reading that is not an invention -- and the decode has already
    // flagged it.
    if( config.mode_reserved )
        return;

    if( config.mode != mMode )
        ++mModeChanges;
    mMode = config.mode;
}

void SessionState::Apply( const Transaction& transaction )
{
    switch( transaction.session.change )
    {
    case SessionChange::None:
        break;

    case SessionChange::GeneralConfigWritten:
    case SessionChange::InbandReset:
        // Both carry the whole register, so both re-establish the state
        // outright -- including clearing an earlier uncertainty, which is
        // exactly what §8.3.2 offers the In-band RESET for. An ACCEPTed write
        // does the same job: the register is written whole, and if the mode
        // had been wrong enough to matter this transaction would not have
        // decoded into a well formed ACCEPT in the first place.
        Adopt( transaction.session.config );
        mUncertain = false;
        break;

    case SessionChange::GeneralConfigUncertain:
        mUncertain = true;
        break;
    }
}

} // namespace espi
