#include "EspiAnalyzerSettings.h"

#include <AnalyzerHelpers.h>

namespace espi_saleae
{
namespace
{

const char* const kIoLabel[ 4 ] = { "I/O[0]", "I/O[1]", "I/O[2]", "I/O[3]" };

const char* const kIoTooltip[ 4 ] = {
    "eSPI I/O[0]. Carries the command phase in Single I/O mode.",
    "eSPI I/O[1]. Carries the response phase in Single I/O mode.",
    "eSPI I/O[2]. Quad I/O mode only.",
    "eSPI I/O[3]. Quad I/O mode only.",
};

double ModeToNumber( espi::IoMode mode )
{
    return static_cast<double>( espi::BitsPerClock( mode ) );
}

espi::IoMode NumberToMode( double number )
{
    if( number >= 4.0 )
        return espi::IoMode::Quad;
    if( number >= 2.0 )
        return espi::IoMode::Dual;
    return espi::IoMode::Single;
}

} // namespace

EspiAnalyzerSettings::EspiAnalyzerSettings()
{
    mChipSelectInterface.reset( new AnalyzerSettingInterfaceChannel() );
    mChipSelectInterface->SetTitleAndTooltip( "CS#", "eSPI Chip Select, active low. Frames one transaction." );
    mChipSelectInterface->SetChannel( mChipSelect );

    mClockInterface.reset( new AnalyzerSettingInterfaceChannel() );
    mClockInterface->SetTitleAndTooltip( "CLK", "eSPI serial clock. Data is sampled on the rising edge." );
    mClockInterface->SetChannel( mClock );

    for( int i = 0; i < 4; ++i )
    {
        mIoInterface[ i ].reset( new AnalyzerSettingInterfaceChannel() );
        mIoInterface[ i ]->SetTitleAndTooltip( kIoLabel[ i ], kIoTooltip[ i ] );
        mIoInterface[ i ]->SetChannel( mIo[ i ] );
        // Single and Dual captures legitimately leave the upper lanes
        // unconnected.
        mIoInterface[ i ]->SetSelectionOfNoneIsAllowed( i >= 2 );
    }

    mModeInterface.reset( new AnalyzerSettingInterfaceNumberList() );
    mModeInterface->SetTitleAndTooltip( "Starting I/O mode",
                                        "I/O mode at the start of the capture. Nothing on the wire states it. "
                                        "Single is the reset default; pick another only for a capture that "
                                        "begins after a SET_CONFIGURATION has already switched the link." );
    mModeInterface->AddNumber( ModeToNumber( espi::IoMode::Single ), "Single (reset default)",
                               "Command on I/O[0], response on I/O[1]." );
    mModeInterface->AddNumber( ModeToNumber( espi::IoMode::Dual ), "Dual", "I/O[1:0], shared half-duplex." );
    mModeInterface->AddNumber( ModeToNumber( espi::IoMode::Quad ), "Quad", "I/O[3:0], shared half-duplex." );
    mModeInterface->SetNumber( ModeToNumber( mStartingMode ) );

    AddInterface( mChipSelectInterface.get() );
    AddInterface( mClockInterface.get() );
    for( int i = 0; i < 4; ++i )
        AddInterface( mIoInterface[ i ].get() );
    AddInterface( mModeInterface.get() );

    AddExportOption( 0, "Export as text/csv file" );
    AddExportExtension( 0, "text", "txt" );
    AddExportExtension( 0, "csv", "csv" );

    ClearChannels();
    AddChannel( mChipSelect, "CS#", false );
    AddChannel( mClock, "CLK", false );
    for( int i = 0; i < 4; ++i )
        AddChannel( mIo[ i ], kIoLabel[ i ], false );
}

EspiAnalyzerSettings::~EspiAnalyzerSettings() = default;

bool EspiAnalyzerSettings::SetSettingsFromInterfaces()
{
    const Channel cs = mChipSelectInterface->GetChannel();
    const Channel clk = mClockInterface->GetChannel();
    Channel io[ 4 ];
    for( int i = 0; i < 4; ++i )
        io[ i ] = mIoInterface[ i ]->GetChannel();

    const espi::IoMode mode = NumberToMode( mModeInterface->GetNumber() );

    if( cs == UNDEFINED_CHANNEL || clk == UNDEFINED_CHANNEL )
    {
        SetErrorText( "CS# and CLK must both be assigned." );
        return false;
    }

    // Single mode needs I/O[1] as well as I/O[0]: the response phase is on a
    // different wire from the command phase. An analyzer configured with only
    // I/O[0] decodes every command and never sees a response, which looks like
    // a dead target rather than a misconfiguration.
    const int lanes_needed = espi::BitsPerClock( mode ) == 1 ? 2 : espi::BitsPerClock( mode );
    for( int i = 0; i < lanes_needed; ++i )
    {
        if( io[ i ] == UNDEFINED_CHANNEL )
        {
            SetErrorText( "This I/O mode needs I/O[0] through "
                          "I/O[3] as shown; assign the lanes it uses." );
            return false;
        }
    }

    Channel assigned[ 6 ];
    U32 count = 0;
    assigned[ count++ ] = cs;
    assigned[ count++ ] = clk;
    for( int i = 0; i < 4; ++i )
    {
        if( io[ i ] != UNDEFINED_CHANNEL )
            assigned[ count++ ] = io[ i ];
    }

    if( AnalyzerHelpers::DoChannelsOverlap( assigned, count ) )
    {
        SetErrorText( "Each eSPI signal needs its own channel." );
        return false;
    }

    mChipSelect = cs;
    mClock = clk;
    for( int i = 0; i < 4; ++i )
        mIo[ i ] = io[ i ];
    mStartingMode = mode;

    ClearChannels();
    AddChannel( mChipSelect, "CS#", true );
    AddChannel( mClock, "CLK", true );
    for( int i = 0; i < 4; ++i )
        AddChannel( mIo[ i ], kIoLabel[ i ], mIo[ i ] != UNDEFINED_CHANNEL );

    return true;
}

void EspiAnalyzerSettings::UpdateInterfacesFromSettings()
{
    mChipSelectInterface->SetChannel( mChipSelect );
    mClockInterface->SetChannel( mClock );
    for( int i = 0; i < 4; ++i )
        mIoInterface[ i ]->SetChannel( mIo[ i ] );
    mModeInterface->SetNumber( ModeToNumber( mStartingMode ) );
}

void EspiAnalyzerSettings::LoadSettings( const char* settings )
{
    SimpleArchive archive;
    archive.SetString( settings );

    archive >> mChipSelect;
    archive >> mClock;
    for( int i = 0; i < 4; ++i )
        archive >> mIo[ i ];

    U32 bits_per_clock = 1;
    archive >> bits_per_clock;
    mStartingMode = NumberToMode( static_cast<double>( bits_per_clock ) );

    ClearChannels();
    AddChannel( mChipSelect, "CS#", true );
    AddChannel( mClock, "CLK", true );
    for( int i = 0; i < 4; ++i )
        AddChannel( mIo[ i ], kIoLabel[ i ], mIo[ i ] != UNDEFINED_CHANNEL );

    UpdateInterfacesFromSettings();
}

const char* EspiAnalyzerSettings::SaveSettings()
{
    SimpleArchive archive;

    archive << mChipSelect;
    archive << mClock;
    for( int i = 0; i < 4; ++i )
        archive << mIo[ i ];
    archive << static_cast<U32>( espi::BitsPerClock( mStartingMode ) );

    return SetReturnString( archive.GetString() );
}

} // namespace espi_saleae
