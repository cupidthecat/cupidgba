#!/usr/bin/env python3
"""Reproduce the final additive identity word from the owned firmware image."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
from pathlib import Path
import re
import struct


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
    expected = f".word 0x{final:08X}\n"
    if args.write:
        INCLUDE.write_text(expected)
        print(f"Generated additive firmware identity word {final:08X}")
    else:
        match = re.fullmatch(r"\.word 0x([0-9A-Fa-f]{8})\s*", INCLUDE.read_text())
        if not match or int(match[1], 16) != final or words[-1] != final:
            raise SystemExit("Firmware additive identity word is stale")
        if sum(words) & 0xFFFFFFFF != IDENTITY:
            raise SystemExit("Firmware additive identity does not match")
        print("Verified all 4096 firmware words and additive identity")


if __name__ == "__main__":
    main()
