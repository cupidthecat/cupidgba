#!/usr/bin/env python3
"""Check that the preferred HLE assembly source reproduces the embedded bytes."""
# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import re
import struct
import sys

source = Path(__file__).resolve().parents[2] / "src/gba/hle-bios.c"
array = source.read_text().split("= {", 1)[1].split("};", 1)[0]
expected = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})\b", array))
actual = Path(sys.argv[1]).read_bytes()
if actual != expected:
    raise SystemExit("HLE assembly output does not match the embedded image")
assembly = source.with_name("bios").joinpath("hle-bios.s").read_text()
offsets = dict(re.findall(r"\.equ\s+(SWI_TABLE_OFFSET|STALL_CALL_OFFSET|UTILITY_SOURCE_OFFSET),\s*(0x[0-9A-Fa-f]+)", assembly))
table = int(offsets["SWI_TABLE_OFFSET"], 16)
stall = int(offsets["STALL_CALL_OFFSET"], 16)
guard = int(offsets["UTILITY_SOURCE_OFFSET"], 16)
if guard != 0xBA4 or struct.unpack_from("<I", actual, guard)[0] != 0xE35C0000:
    raise SystemExit("HLE observable utility guard address is invalid")
if len(actual) > 0x4000 or struct.unpack_from("<I", actual, stall)[0] != 0xE25BB004:
    raise SystemExit("HLE image size or internal stall entry is invalid")
for call in (1, *range(6, 13), *range(0x0E, 0x1A)):
    target = struct.unpack_from("<I", actual, table + call * 4)[0]
    aligned = target & ~1
    valid_mode = target & 1 if call in (1, 0x0A, 0x0B, *range(0x14, 0x1A)) else target % 4 == 0
    if target == stall or not valid_mode or not table + 0x2B * 4 <= aligned < len(actual):
        raise SystemExit("HLE timed-service dispatch target is invalid")
print(f"Verified {len(actual)} HLE bytes")
