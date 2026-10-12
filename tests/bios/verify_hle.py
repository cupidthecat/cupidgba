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
offsets = dict(re.findall(r"\.equ\s+(SWI_TABLE_OFFSET|STALL_CALL_OFFSET),\s*(0x[0-9A-Fa-f]+)", assembly))
table = int(offsets["SWI_TABLE_OFFSET"], 16)
stall = int(offsets["STALL_CALL_OFFSET"], 16)
# These five dispatch slots share the internal stall-count routine.
for call in range(6, 11):
    if struct.unpack_from("<I", actual, table + call * 4)[0] != stall:
        raise SystemExit("HLE dispatch offsets do not match the assembled table")
print(f"Verified {len(actual)} HLE bytes")
