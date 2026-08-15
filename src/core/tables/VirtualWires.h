#ifndef ESPI_TABLE_VIRTUAL_WIRES_H
#define ESPI_TABLE_VIRTUAL_WIRES_H

// ---------------------------------------------------------------------------
//  VIRTUAL WIRES
//
//  SOURCE   eSPI Interface Base Specification
//           Section 4.2.2 "Virtual Wires Channel", p.57 -- the count byte
//           Table 8: "Virtual Wire Index Definition", pp.60-62
//           Section 4.2.2.2 "System Event Virtual Wires", p.62
//           Section 4.2.2.4 "Interrupt Event", pp.69-70
//           Section 4.2.2.5 "General-Purpose I/O Expander", p.72
//           Table 9: "System Event Virtual Wires for Index=2", p.63
//           Table 10: "System Event Virtual Wires for Index=3", p.64
//           Table 11: "System Event Virtual Wires for Index=4", p.65
//           Table 12: "System Event Virtual Wires for Index=5", pp.66-67
//           Table 13: "System Event Virtual Wires for Index=6", pp.67-68
//           Table 14: "System Event Virtual Wires for Index=7", pp.68-69
//
//  TWO DATA FORMATS, NOT ONE. Table 8 gives the System Event and GPIO Expander
//  ranges the familiar valid/level byte, but indices 0 and 1 are Interrupt
//  events and are laid out completely differently -- bit 7 is the interrupt
//  level and bits 6:0 are the IRQ line number. Decoding an interrupt event as
//  valid/level produces four plausible-looking wires out of an IRQ number.
//
//  WHAT THE COMPATIBILITY SPECIFICATION WOULD ADD, and does not, here. The plan
//  names Compatibility spec Table 2-13 as the authority on index assignments.
//  That document is not in this database, so nothing below comes from it. The
//  base spec is self-consistent about the consequence: indices 64-127 are
//  "Platform specific ... defined in the respective platform specific documents
//  and outside the scope of the specification" (p.62), and 8-63 are Reserved.
//  Both land as an explicit gap -- the decoder names the range and stops. This
//  is not hypothetical: the capture contains a real GET_VWIRE answering with
//  index 40h, which is inside the platform specific range.
//
//  TRANSCRIPTION HAZARDS FOUND WHILE READING THESE PAGES:
//
//    - The chunk store files the Index=4 wires (PME#, WAKE#, OOB_RST_ACK) under
//      the section heading "Table 10: System Event Virtual Wires for Index=3"
//      and reports them on p.65. Table 10 is Index=3 on p.64; those wires are
//      Table 11. Both the table number and the page are wrong in the metadata.
//    - The same extraction merges the Valid rows of every one of these tables
//      into a single cell with the bit numbers detached from the wire names.
//    - Table 8 arrives with its two data-format diagrams flattened into one
//      row, so the Interrupt layout and the valid/level layout are impossible
//      to tell apart in the text.
//
//  NAME DRIFT ACROSS SPEC REVISIONS. Revision 1.6 calls index 5 bit 3
//  TARGET_BOOT_LOAD_STATUS. Older material -- including the worked example in
//  docs/PLAN.md section 7 -- calls the same wire SLAVE_BOOT_LOAD_STATUS. The
//  encoding is unchanged; only the word is. The names below are the ones on the
//  rendered pages of the document this repository actually cites.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
//  THE VIRTUAL WIRE COUNT -- section 4.2.2, p.57.
//
//  "The 6-bit count field allows up to 64 Virtual Wire groups to be
//  communicated in the same packet. This is a 0-based count." Bits 7:6 are
//  Reserved, so a set bit there is reported rather than masked away -- exactly
//  as the reserved bits of the status register and the config address are.
// ---------------------------------------------------------------------------
#define ESPI_VWIRE_COUNT_MASK 0x3Fu
#define ESPI_VWIRE_COUNT_RESERVED_MASK 0xC0u
#define ESPI_VWIRE_MAX_GROUPS 64u

// ---------------------------------------------------------------------------
//  THE VALID/LEVEL PAIRING -- Table 8, p.61.
//
//  "Valid: This field indicates the validity of the 1-to-1 corresponding Level
//  bits." Bits 7:4 are the valid bits, bits 3:0 the levels, so the valid bit
//  for level bit n is n + 4.
//
//  This is stated once here rather than repeated as a column on all 24 rows of
//  the wire table below. Every per-index table restates the pairing in prose --
//  "SLP_S5# Valid: This bit indicates the validity of SLP_S5# virtual wire on
//  bit[2]" on bit 6, and so on -- and all 24 restatements agree with the rule,
//  including the three reserved pairs (index 2 bit 7 over RSV at bit 3, index 4
//  bit 5 over RSV at bit 1, index 7 bit 7 over RSV at bit 3).
//
//  A valid bit of '0' is a mask, not an absence: "the corresponding virtual
//  wire must retain its previous value and it must not be updated for this
//  virtual wire packet." So a level bit under a clear valid bit is a stale
//  value being echoed, not a wire state. The capture contains exactly that --
//  index 05h data 19h has bit 3 set with bit 7 clear.
// ---------------------------------------------------------------------------
//  The valid nibble is not a separate constant: bits 7:4 are exactly the level
//  mask shifted up by the pairing rule, and a second constant saying the same
//  thing is one that can drift away from the first.
#define ESPI_VWIRE_VALID_SHIFT 4u
#define ESPI_VWIRE_LEVEL_MASK 0x0Fu

// ---------------------------------------------------------------------------
//  THE INTERRUPT EVENT DATA FORMAT -- Table 8, p.60.
//
//  Bit 7 is the Interrupt Level, bits 6:0 the Interrupt Line. The index selects
//  which bank of 128 the line falls in: "Index=0h: IRQ 0 - 127, Index=1h: IRQ
//  128 - 255", so the IRQ number is index * 128 + line.
//
//  Two further sentences from the same cell, both load bearing: "Interrupt
//  event virtual wires are defined from target to controller only" and
//  "Interrupt level high ('1') indicates interrupt assertion. Interrupt events
//  virtual wires are active high."
//
//  Section 4.2.2.4 restates both on p.70, and Table 15 on the same page titles
//  its result column "Target to Controller IRQ Virtual Wire (Active High)".
//  Direction and polarity for this range are therefore stated twice, in two
//  sections, in agreement -- which is worth noting because it is the only part
//  of this header where that is true.
// ---------------------------------------------------------------------------
#define ESPI_VWIRE_IRQ_LEVEL_BIT 7u
#define ESPI_VWIRE_IRQ_LINE_MASK 0x7Fu
#define ESPI_VWIRE_IRQ_BANK 128u

// ---------------------------------------------------------------------------
//  INDEX RANGES -- Table 8, pp.60-62, every row in page order.
//
//  X( START, END, GROUP, FORMAT, DIRECTION, RESET )
//
//  FORMAT is ours, not the page's:
//
//    Interrupt   bit 7 level, bits 6:0 IRQ line
//    ValidLevel  bits 7:4 valid, bits 3:0 level
//    NotDefined  the base specification gives the range no data format at all
//
//  DIRECTION is stated in the table cell for the interrupt range, per index for
//  the System Events (see the table below), and in section 4.2.2.5 for the GPIO
//  Expander -- see the block below, because that one is not a fixed value.
//
//  RESET is empty for every range whose reset domain the specification states
//  per index instead, or does not state at all.
//
//  THE GPIO EXPANDER'S DIRECTION IS CONFIGURED, NOT FIXED -- section 4.2.2.5,
//  p.72. Two separate facts, and the first is what explains the second:
//
//    1. The pins are always physically on the target. "The specification allows
//       the eSPI controller to claim the General-Purpose I/O (GPIO) pins
//       physically resided on the eSPI target side as part of its own virtual
//       I/O pins." The controller is never the expander.
//
//    2. Which way a given index's messages travel depends on how that index is
//       configured. An index configured as *output* pins has "the eSPI
//       controller tunnel the state of the Virtual GPIO pin" for the target to
//       reflect on its physical pin -- controller to target. An index
//       configured as *input* pins has "the eSPI target sample the state of
//       the physical GPIO pin and then tunnel the state" -- target to
//       controller. Both are the same pin on the target being driven or read.
//
//  "All the GPIO pins sharing the same index number must be configured to the
//  same direction ... either all inputs or all outputs, but not a combination",
//  so the direction is uniform within an index -- but the configuration itself
//  is implementation specific and never appears on the bus. Configurable is
//  therefore the honest answer, and it is a different answer from Unspecified:
//  the specification addresses this range's direction and says it varies.
//
//  The same paragraph gives the range its reset domain: "a group of Virtual
//  GPIOs sharing the same index will share the same reset. The reset is
//  programmable to be reset by either eSPI Reset# or Platform Reset."
// ---------------------------------------------------------------------------
#define ESPI_VWIRE_INDEX_TABLE( X )                                                                                                \
    X( 0, 1, "Interrupt event", Interrupt, TargetToController, "" )                                                                \
    X( 2, 7, "System Event", ValidLevel, Unspecified, "" )                                                                         \
    X( 8, 63, "Reserved", NotDefined, Unspecified, "" )                                                                            \
    X( 64, 127, "Platform specific", NotDefined, Unspecified, "" )                                                                 \
    X( 128, 255, "General Purpose I/O Expander", ValidLevel, Configurable, "programmable: eSPI Reset# or Platform Reset" )

// ---------------------------------------------------------------------------
//  SYSTEM EVENT INDICES -- the four-row header block at the top of Tables 9-14.
//
//  X( INDEX, RESET, DIRECTION )
//
//  RESET is the reset domain the group belongs to, not a value: indices 2-5 are
//  reset by eSPI Reset# and 6-7 by PLTRST#. DIRECTION alternates in a way that
//  is easy to get backwards, so it is transcribed per index rather than
//  inferred from the wire names.
//
//  Note 1 on p.63 qualifies index 2 only: "Depending on the usage, the state of
//  these virtual wires may need to be retained in deeper power well such that
//  they are not reset by eSPI Reset#."
// ---------------------------------------------------------------------------
#define ESPI_VWIRE_SYSTEM_EVENT_INDEX_TABLE( X )                                                                                   \
    X( 2, "eSPI Reset#", ControllerToTarget )                                                                                      \
    X( 3, "eSPI Reset#", ControllerToTarget )                                                                                      \
    X( 4, "eSPI Reset#", TargetToController )                                                                                      \
    X( 5, "eSPI Reset#", TargetToController )                                                                                      \
    X( 6, "PLTRST#", TargetToController )                                                                                          \
    X( 7, "PLTRST#", ControllerToTarget )

// ---------------------------------------------------------------------------
//  SYSTEM EVENT WIRES -- Tables 9-14, pp.63-69.
//
//  X( INDEX, BIT, NAME, POLARITY, RESET )
//
//  BIT is the level bit, 3 down to 0, in the order the pages print them. The
//  valid bit that gates it is BIT + 4; see the pairing block above.
//
//  POLARITY  ActiveHigh / ActiveLow, from the "Polarity:" line of each cell.
//            AsDefined for TARGET_BOOT_LOAD_STATUS, whose cell says "Polarity:
//            As defined above" because '0' and '1' mean a corrupted and an
//            intact boot image rather than an asserted and a released signal.
//            None on the reserved rows.
//  RESET     the "Reset:" line: Active or Inactive, or Zero where the page
//            prints Reset: '0'. None on the reserved rows.
//
//  The polarity is what turns a level bit into a statement about the platform:
//  SLP_S3# at level '0' is S3 sleep being *requested*, and rendering that as
//  "low" and stopping would leave the reader doing the inversion by hand.
//
//  RSV rows are listed so that every one of the eight bits is accounted for and
//  the table can be checked against the page row by row. The specification
//  gives no polarity or reset for them and neither does this table.
// ---------------------------------------------------------------------------
#define ESPI_VWIRE_SYSTEM_EVENT_TABLE( X )                                                                                         \
    /* --- Index 2, Table 9, p.63 --- */                                                                                           \
    X( 2, 3, "RSV", None, None )                                                                                                   \
    X( 2, 2, "SLP_S5#", ActiveLow, Active )                                                                                        \
    X( 2, 1, "SLP_S4#", ActiveLow, Active )                                                                                        \
    X( 2, 0, "SLP_S3#", ActiveLow, Active )                                                                                        \
    /* --- Index 3, Table 10, p.64 --- */                                                                                          \
    X( 3, 3, "RSV", None, None )                                                                                                   \
    X( 3, 2, "OOB_RST_WARN", ActiveHigh, Inactive )                                                                                \
    X( 3, 1, "PLTRST#", ActiveLow, Active )                                                                                        \
    X( 3, 0, "SUS_STAT#", ActiveLow, Active )                                                                                      \
    /* --- Index 4, Table 11, p.65 --- */                                                                                          \
    X( 4, 3, "PME#", ActiveLow, Inactive )                                                                                         \
    X( 4, 2, "WAKE#", ActiveLow, Inactive )                                                                                        \
    X( 4, 1, "RSV", None, None )                                                                                                   \
    X( 4, 0, "OOB_RST_ACK", ActiveHigh, Inactive )                                                                                 \
    /* --- Index 5, Table 12, pp.66-67 --- */                                                                                      \
    X( 5, 3, "TARGET_BOOT_LOAD_STATUS", AsDefined, Zero )                                                                          \
    X( 5, 2, "ERROR_NONFATAL", ActiveHigh, Inactive )                                                                              \
    X( 5, 1, "ERROR_FATAL", ActiveHigh, Inactive )                                                                                 \
    X( 5, 0, "TARGET_BOOT_LOAD_DONE", ActiveHigh, Inactive )                                                                       \
    /* --- Index 6, Table 13, pp.67-68 --- */                                                                                      \
    X( 6, 3, "HOST_RST_ACK", ActiveHigh, Inactive )                                                                                \
    X( 6, 2, "RCIN#", ActiveLow, Inactive )                                                                                        \
    X( 6, 1, "SMI#", ActiveLow, Inactive )                                                                                         \
    X( 6, 0, "SCI#", ActiveLow, Inactive )                                                                                         \
    /* --- Index 7, Table 14, pp.68-69 --- */                                                                                      \
    X( 7, 3, "RSV", None, None )                                                                                                   \
    X( 7, 2, "NMIOUT#", ActiveLow, Inactive )                                                                                      \
    X( 7, 1, "SMIOUT#", ActiveLow, Inactive )                                                                                      \
    X( 7, 0, "HOST_RST_WARN", ActiveHigh, Inactive )

#endif // ESPI_TABLE_VIRTUAL_WIRES_H
