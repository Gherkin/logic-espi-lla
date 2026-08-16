#include "SimulationScript.h"

namespace espi_saleae
{

// Eight transactions off real hardware, in the order tests/vectors/espi_dump.txt
// has them: the controller reads and writes a configuration register, collects
// a virtual wire group, brings the OOB channel up, and picks up the EC's boot
// status on the way. See SimulationScript.h for why these are literal bytes.
const std::vector<SimTransaction>& SimulationScript()
{
    static const std::vector<SimTransaction> script = {
        // espi_dump.txt lines 16-29. Address out MSB first, DWord back LSB
        // first (section 5.1, p.86).
        { { 0x21, 0x00, 0x20, 0xC8 },
          true,
          { 0x08, 0x00, 0x0B, 0x00, 0x00, 0x0F, 0x01, 0x91 },
          "get_configuration.espi",
          0,
          "GET_CONFIGURATION 0020h -- Device Identification" },

        // espi_dump.txt lines 30-43. The mirror image: the command carries the
        // DWord and the response carries only the status trailer.
        { { 0x22, 0x00, 0x20, 0x01, 0x00, 0x07, 0x00, 0x01 },
          true,
          { 0x08, 0x0F, 0x01, 0x95 },
          "set_configuration.espi",
          0,
          "SET_CONFIGURATION 0020h" },

        // espi_dump.txt lines 72-82. One virtual wire group at index 05h.
        { { 0x05, 0x1B },
          true,
          { 0x08, 0x00, 0x05, 0x88, 0x0F, 0x01, 0x9F },
          "get_vwire.espi",
          0,
          "GET_VWIRE -- target boot load status" },

        // espi_dump.txt lines 83-96. Channel 2 capabilities: 64 byte payload,
        // channel neither ready nor enabled.
        { { 0x21, 0x00, 0x30, 0xB8 },
          true,
          { 0x08, 0x10, 0x01, 0x00, 0x00, 0x0F, 0x01, 0xD2 },
          "config_oob_channel.espi",
          0,
          "GET_CONFIGURATION 0030h -- OOB channel capabilities" },

        // espi_dump.txt lines 97-110, consecutive on the bus with the read
        // above: bit 0 set to enable the channel.
        { { 0x22, 0x00, 0x30, 0x01, 0x01, 0x00, 0x00, 0x33 },
          true,
          { 0x08, 0x0F, 0x01, 0x95 },
          "config_oob_channel.espi",
          1,
          "SET_CONFIGURATION 0030h -- enable the OOB channel" },

        // espi_dump.txt lines 265-275. Data 19h: the valid nibble covers only
        // bit[0], so bit[3] is a stale echo rather than a wire state.
        { { 0x05, 0x1B },
          true,
          { 0x08, 0x00, 0x05, 0x19, 0x0F, 0x01, 0x5D },
          "get_vwire_boot_done.espi",
          0,
          "GET_VWIRE -- target boot load done" },

        // espi_dump.txt lines 458-468. Index 40h is platform specific and the
        // base specification does not name its bits.
        { { 0x05, 0x1B },
          true,
          { 0x08, 0x00, 0x40, 0x11, 0x0F, 0x01, 0xD9 },
          "get_vwire_platform_index.espi",
          0,
          "GET_VWIRE -- platform specific index 40h" },

        // espi_dump.txt lines 651-661. The same index a tenth of a millisecond
        // later, with the data byte changed from 11h to 10h.
        { { 0x05, 0x1B },
          true,
          { 0x08, 0x00, 0x40, 0x10, 0x0F, 0x01, 0xB2 },
          "get_vwire_platform_index.espi",
          1,
          "GET_VWIRE -- platform specific index 40h, later" },
    };
    return script;
}

} // namespace espi_saleae
