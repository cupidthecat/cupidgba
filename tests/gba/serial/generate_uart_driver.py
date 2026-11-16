#!/usr/bin/env python3
# @file      generate_uart_driver.py
# @author    cupidthecat
# @brief     Generate or verify the embedded image of the owned UART driver.
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
"""Generate or verify the embedded image of the owned UART driver."""
from pathlib import Path
import argparse


FILE_HEADER = (
    '/**\n'
    ' * @file      uart-driver.inc\n'
    ' * @author    cupidthecat\n'
    ' * @brief     Owned ARM/Thumb guest UART transmit and receive driver.\n'
    ' * @date      2026-11-16\n'
    ' *\n'
    ' * @copyright Copyright (c) 2026 cupidthecat\n'
    ' *\n'
    ' * This file is part of CupidGBA.\n'
    ' *\n'
    ' * CupidGBA is free software: you can redistribute it and/or modify\n'
    ' * it under the terms of the GNU General Public License as published by\n'
    ' * the Free Software Foundation, either version 3 of the License, or\n'
    ' * (at your option) any later version.\n'
    ' *\n'
    ' * CupidGBA is distributed in the hope that it will be useful,\n'
    ' * but WITHOUT ANY WARRANTY; without even the implied warranty of\n'
    ' * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n'
    ' * GNU General Public License for more details.\n'
    ' *\n'
    ' * You should have received a copy of the GNU General Public License\n'
    ' * along with CupidGBA.  If not, see <https://gnu.org>.\n'
    ' */\n'
)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("image", type=Path)
parser.add_argument("--write", action="store_true")
args = parser.parse_args()
image = args.image.read_bytes()
if len(image) != 512 or image[0xFC:0x100] != bytes.fromhex("feffffea") or image[0x1FC:0x1FE] != bytes.fromhex("fee7"):
    raise SystemExit("UART driver size or terminal differs")
rows = ["/* Generated from uart-driver.s; see the serial documentation. */",
        "static const uint8_t uartDriver[] = {"]
for offset in range(0, len(image), 16):
    rows.append("    " + ", ".join(f"0x{value:02X}" for value in image[offset:offset + 16]) + ",")
rows.append("};")
expected = ("\n".join(rows) + "\n").encode()
expected = FILE_HEADER.encode() + expected
target = Path(__file__).with_name("uart-driver.inc")
if args.write:
    target.write_bytes(expected)
elif target.read_bytes() != expected:
    raise SystemExit("UART driver assembly output differs from the embedded fixture")
print(f"Verified {len(image)} owned UART driver bytes")
