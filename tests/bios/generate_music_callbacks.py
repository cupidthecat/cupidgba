#!/usr/bin/env python3
"""Generate or verify the byte image of the owned ARM/Thumb music callbacks."""
# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import argparse

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("image", type=Path)
parser.add_argument("--write", action="store_true")
args = parser.parse_args()
image = args.image.read_bytes()
if not 0x600 < len(image) < 0xF00:
    raise SystemExit("Callback image exceeds its owned code region")
rows = ["/* SPDX-License-Identifier: GPL-3.0-or-later */",
        "/* Generated from music-callbacks.s; see the BIOS rebuilding guide. */",
        "static const uint8_t musicCallbacks[] = {"]
for offset in range(0, len(image), 12):
    rows.append("    " + ", ".join(f"0x{value:02X}" for value in image[offset:offset + 12]) + ",")
rows.append("};")
expected = ("\n".join(rows) + "\n").encode()
target = Path(__file__).with_name("music-callbacks.inc")
if args.write:
    target.write_bytes(expected)
elif target.read_bytes() != expected:
    raise SystemExit("Callback assembly output does not match the embedded fixture")
print(f"Verified {len(image)} owned callback bytes")
