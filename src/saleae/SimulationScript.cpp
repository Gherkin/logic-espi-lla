#include "SimulationScript.h"

namespace espi_saleae
{

namespace
{

// The tail of the script that only replays from a Single I/O start with all
// four lanes wired -- see SimulationScriptFrom in the header for both reasons.
const size_t kModeExcursionEntries = 3;

} // namespace

// Eight transactions off real hardware, in the order tests/vectors/espi_dump.txt
// has them: the controller reads and writes a configuration register, collects
// a virtual wire group, brings the OOB channel up, and picks up the EC's boot
// status on the way. Then three hand-built ones that take the link to Quad I/O
// and reset it back. See SimulationScript.h for why these are literal bytes and
// why each states its own mode.
const std::vector<SimTransaction>& SimulationScript()
{
    static const std::vector<SimTransaction> script = {
        // espi_dump.txt lines 16-29. Address out MSB first, DWord back LSB
        // first (section 5.1, p.86).
        { { 0x21, 0x00, 0x20, 0xC8 },
          true,
          { 0x08, 0x00, 0x0B, 0x00, 0x00, 0x0F, 0x01, 0x91 },
          espi::IoMode::Single,
          "get_configuration.espi",
          0,
          "GET_CONFIGURATION 0020h -- Device Identification" },

        // espi_dump.txt lines 30-43. The mirror image: the command carries the
        // DWord and the response carries only the status trailer.
        { { 0x22, 0x00, 0x20, 0x01, 0x00, 0x07, 0x00, 0x01 },
          true,
          { 0x08, 0x0F, 0x01, 0x95 },
          espi::IoMode::Single,
          "set_configuration.espi",
          0,
          "SET_CONFIGURATION 0020h" },

        // espi_dump.txt lines 72-82. One virtual wire group at index 05h.
        { { 0x05, 0x1B },
          true,
          { 0x08, 0x00, 0x05, 0x88, 0x0F, 0x01, 0x9F },
          espi::IoMode::Single,
          "get_vwire.espi",
          0,
          "GET_VWIRE -- target boot load status" },

        // espi_dump.txt lines 83-96. Channel 2 capabilities: 64 byte payload,
        // channel neither ready nor enabled.
        { { 0x21, 0x00, 0x30, 0xB8 },
          true,
          { 0x08, 0x10, 0x01, 0x00, 0x00, 0x0F, 0x01, 0xD2 },
          espi::IoMode::Single,
          "config_oob_channel.espi",
          0,
          "GET_CONFIGURATION 0030h -- OOB channel capabilities" },

        // espi_dump.txt lines 97-110, consecutive on the bus with the read
        // above: bit 0 set to enable the channel.
        { { 0x22, 0x00, 0x30, 0x01, 0x01, 0x00, 0x00, 0x33 },
          true,
          { 0x08, 0x0F, 0x01, 0x95 },
          espi::IoMode::Single,
          "config_oob_channel.espi",
          1,
          "SET_CONFIGURATION 0030h -- enable the OOB channel" },

        // espi_dump.txt lines 265-275. Data 19h: the valid nibble covers only
        // bit[0], so bit[3] is a stale echo rather than a wire state.
        { { 0x05, 0x1B },
          true,
          { 0x08, 0x00, 0x05, 0x19, 0x0F, 0x01, 0x5D },
          espi::IoMode::Single,
          "get_vwire_boot_done.espi",
          0,
          "GET_VWIRE -- target boot load done" },

        // espi_dump.txt lines 458-468. Index 40h is platform specific and the
        // base specification does not name its bits.
        { { 0x05, 0x1B },
          true,
          { 0x08, 0x00, 0x40, 0x11, 0x0F, 0x01, 0xD9 },
          espi::IoMode::Single,
          "get_vwire_platform_index.espi",
          0,
          "GET_VWIRE -- platform specific index 40h" },

        // espi_dump.txt lines 651-661. The same index a tenth of a millisecond
        // later, with the data byte changed from 11h to 10h.
        { { 0x05, 0x1B },
          true,
          { 0x08, 0x00, 0x40, 0x10, 0x0F, 0x01, 0xB2 },
          espi::IoMode::Single,
          "get_vwire_platform_index.espi",
          1,
          "GET_VWIRE -- platform specific index 40h, later" },

        // --- the mode excursion, hand built ------------------------------
        //
        // Everything above is Single I/O because the capture is. These three
        // are not from the capture and say so in their fixtures' own headers.

        // Offset 0008h with I/O Mode Select set to 10b, ACCEPTed. Sent in
        // Single I/O: "The SET_CONFIGURATION is completed with the current mode
        // of operation" (§5.1, p.86), so this transaction is the last one at
        // the old geometry and the change lands on its deassertion edge.
        { { 0x22, 0x00, 0x08, 0x0F, 0x00, 0x34, 0x8B, 0xC6 },
          true,
          { 0x08, 0x0F, 0x01, 0x95 },
          espi::IoMode::Single,
          "set_configuration_io_mode.espi",
          0,
          "SET_CONFIGURATION 0008h -- switch to Quad I/O" },

        // The first transaction on the other side of the switch, and the same
        // bytes as the script's opening one. Four bits per clock now, so it is
        // a quarter of the width on screen -- and only the analyzer's own
        // session state says which it is.
        { { 0x21, 0x00, 0x20, 0xC8 },
          true,
          { 0x08, 0x00, 0x0B, 0x00, 0x00, 0x0F, 0x01, 0x91 },
          espi::IoMode::Quad,
          "get_configuration.espi",
          0,
          "GET_CONFIGURATION 0020h -- now in Quad I/O" },

        // An In-band RESET, which puts 008h-00Bh back to its default and the
        // link back to Single I/O (§8.3.2, p.123). It is what closes the loop:
        // the script wraps to a Single I/O transaction, so the mode it ends in
        // has to be the mode it starts in.
        //
        // Sixteen clocks is EIGHT BYTES in Quad, which is why this cites
        // reset_quad.espi and not reset.espi.
        { { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
          false,
          {},
          espi::IoMode::Quad,
          "reset_quad.espi",
          0,
          "In-band RESET -- back to Single I/O" },
    };
    return script;
}

std::vector<SimTransaction> SimulationScriptFrom( espi::IoMode starting_mode, int lanes_assigned )
{
    const std::vector<SimTransaction>& script = SimulationScript();

    if( starting_mode == espi::IoMode::Single && lanes_assigned == 4 )
        return script;

    // Without the excursion the remaining transactions are mode independent --
    // no RESET, whose length is stated in clocks, and no write to 008h -- so
    // they can be laid down at whatever mode the run starts in and decode to
    // the same text. That is what the demo did before phase 7.
    std::vector<SimTransaction> base( script.begin(), script.end() - kModeExcursionEntries );
    for( SimTransaction& entry : base )
        entry.mode = starting_mode;
    return base;
}

} // namespace espi_saleae
