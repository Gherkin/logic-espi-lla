#!/usr/bin/env python3
"""Out-of-tree eSPI CRC-8 reference. Rule R4.

Deliberately NOT linked into the analyzer or the test suite. Its only job is
to produce CRC bytes for hand-written fixtures, so that in-tree CRC code never
generates the data that in-tree CRC code is then tested against.

    ./tools/crc_ref.py 21 00 20
    0xC8

Honest limitation: this is a separate implementation by the same author, so it
catches transcription slips, not a shared misconception about which bytes a
given phase's CRC actually covers. The independent pin for span correctness is
tests/vectors/espi_dump.txt -- real bus traffic from a third-party decoder.

Parameters, from eSPI Base Specification section 5.2, p.90:
    polynomial  x^8 + x^2 + x + 1  (0x07)
    seed        0x00
    bit order   MSB first, no reflection, no final XOR
"""

import sys


def crc8(data, poly=0x07, seed=0x00):
    crc = seed
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ poly) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def main(argv):
    if not argv:
        print(__doc__.strip())
        return 1
    try:
        data = [int(tok, 16) for tok in argv]
    except ValueError:
        print(f"error: arguments must be hex bytes, got {argv!r}", file=sys.stderr)
        return 2
    if any(b < 0 or b > 0xFF for b in data):
        print("error: values must be in range 00..FF", file=sys.stderr)
        return 2
    print(f"0x{crc8(data):02X}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
