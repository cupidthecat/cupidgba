#!/usr/bin/env python3
# @file      generate_checksum_identity.py
# @author    cupidthecat
# @brief     Reproduce the final additive identity word from the owned firmware image.
# @date      2026-11-16
#
# @copyright Copyright (c) 2026 cupidthecat
#
# This file is part of CupidGBA.
#
# CupidGBA is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# CupidGBA is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with CupidGBA.  If not, see <https://gnu.org>.
"""Reproduce the final additive identity word from the owned firmware image."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
from pathlib import Path
import re
import struct


FILE_HEADER = (
    '@ @file      checksum-identity.inc\n'
    '@ @author    cupidthecat\n'
    '@ @brief     Generated additive identity word for the complete replacement BIOS.\n'
    '@ @date      2026-11-16\n'
    '@\n'
    '@ @copyright Copyright (c) 2026 cupidthecat\n'
    '@\n'
    '@ This file is part of CupidGBA.\n'
    '@\n'
    '@ CupidGBA is free software: you can redistribute it and/or modify\n'
    '@ it under the terms of the GNU General Public License as published by\n'
    '@ the Free Software Foundation, either version 3 of the License, or\n'
    '@ (at your option) any later version.\n'
    '@\n'
    '@ CupidGBA is distributed in the hope that it will be useful,\n'
    '@ but WITHOUT ANY WARRANTY; without even the implied warranty of\n'
    '@ MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n'
    '@ GNU General Public License for more details.\n'
    '@\n'
    '@ You should have received a copy of the GNU General Public License\n'
    '@ along with CupidGBA.  If not, see <https://gnu.org>.\n'
)


IDENTITY = 0xBAAE187F
SIZE = 0x4000
INCLUDE = Path(__file__).resolve().parents[2] / "src/gba/bios/checksum-identity.inc"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--write", action="store_true")
    action.add_argument("--check", action="store_true")
    args = parser.parse_args()
    image = args.image.read_bytes()
    if len(image) != SIZE:
        raise SystemExit("Firmware identity requires exactly 16 KiB")
    words = struct.unpack("<4096I", image)
    final = (IDENTITY - sum(words[:-1])) & 0xFFFFFFFF
    expected = FILE_HEADER + f".word 0x{final:08X}\n"
    if args.write:
        INCLUDE.write_text(expected)
        print(f"Generated additive firmware identity word {final:08X}")
    else:
        text = INCLUDE.read_text()
        match = re.fullmatch(r"\.word 0x([0-9A-Fa-f]{8})\s*", text[len(FILE_HEADER):])
        if text != expected or not match or int(match[1], 16) != final or words[-1] != final:
            raise SystemExit("Firmware additive identity word is stale")
        if sum(words) & 0xFFFFFFFF != IDENTITY:
            raise SystemExit("Firmware additive identity does not match")
        print("Verified all 4096 firmware words and additive identity")


if __name__ == "__main__":
    main()
