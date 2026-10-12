#!/usr/bin/env python3
"""Check that the preferred HLE assembly source reproduces the embedded bytes."""
# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import re
import sys

source = Path(__file__).resolve().parents[2] / "src/gba/hle-bios.c"
array = source.read_text().split("= {", 1)[1].split("};", 1)[0]
expected = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})\b", array))
actual = Path(sys.argv[1]).read_bytes()
if actual != expected:
    raise SystemExit("HLE assembly output does not match the embedded image")
print(f"Verified {len(actual)} HLE bytes")
