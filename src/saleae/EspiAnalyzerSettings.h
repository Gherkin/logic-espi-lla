#ifndef ESPI_ANALYZER_SETTINGS_H
#define ESPI_ANALYZER_SETTINGS_H

#include "espi/IoMode.h"

#include <AnalyzerSettingInterface.h>
#include <AnalyzerSettings.h>

#include <memory>

namespace espi_saleae
{

// ---------------------------------------------------------------------------
//  Settings UI.
//
//  Six channels, and one thing the wire cannot tell us.
//
//  WHY THE I/O MODE IS A SETTING AT ALL. Nothing on the bus announces it.
//  A link decodes as Single I/O out of reset and switches to Dual or Quad the
//  moment a SET_CONFIGURATION to offset 08h is accepted -- so a capture that
//  starts at reset needs no help, but one that starts mid-session cannot be
//  decoded from the waveform alone. The default is the reset state, which
//  espi::ConfigResetValue() in the core already establishes as Single I/O with
//  CRC checking disabled; the setting exists for captures that begin later.
//
//  IT IS THE STARTING POINT AND NOTHING MORE. From the first chip select
//  onward the wire decides: espi::SessionState follows an accepted write of
//  008h and an In-band RESET, and EspiAnalyzer::WorkerThread hands the result
//  to SamplingByteSource::SetMode() before each transaction. Leaving this at
//  Single I/O for a capture that begins at reset is correct even if the link
//  goes to Quad three transactions in.
// ---------------------------------------------------------------------------

class EspiAnalyzerSettings : public AnalyzerSettings
{
  public:
    EspiAnalyzerSettings();
    ~EspiAnalyzerSettings() override;

    bool SetSettingsFromInterfaces() override;
    void LoadSettings( const char* settings ) override;
    const char* SaveSettings() override;

    void UpdateInterfacesFromSettings();

    Channel mChipSelect = UNDEFINED_CHANNEL;
    Channel mClock = UNDEFINED_CHANNEL;
    Channel mIo[ 4 ] = { UNDEFINED_CHANNEL, UNDEFINED_CHANNEL, UNDEFINED_CHANNEL, UNDEFINED_CHANNEL };
    espi::IoMode mStartingMode = espi::IoMode::Single;

  private:
    std::unique_ptr<AnalyzerSettingInterfaceChannel> mChipSelectInterface;
    std::unique_ptr<AnalyzerSettingInterfaceChannel> mClockInterface;
    std::unique_ptr<AnalyzerSettingInterfaceChannel> mIoInterface[ 4 ];
    std::unique_ptr<AnalyzerSettingInterfaceNumberList> mModeInterface;
};

} // namespace espi_saleae

#endif // ESPI_ANALYZER_SETTINGS_H
