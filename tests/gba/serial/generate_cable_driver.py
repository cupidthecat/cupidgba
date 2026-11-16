#!/usr/bin/env python3
# @file      generate_cable_driver.py
# @author    cupidthecat
# @brief     Verify or generate the complete owned local-cable driver image.
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
"""Verify or generate the complete owned local-cable driver image."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
from pathlib import Path


FILE_HEADER = (
    '/**\n'
    ' * @file      cable-driver.inc\n'
    ' * @author    cupidthecat\n'
    ' * @brief     Owned normal and multiplayer guest callers for local-cable checks.\n'
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
parser.add_argument('image', type=Path)
parser.add_argument('--write', action='store_true')
args = parser.parse_args()
image = args.image.read_bytes()
if len(image) != 512 or any(image[offset:offset + 4] != bytes.fromhex('feffffea') for offset in (0xFC, 0x1FC)):
    raise SystemExit('Cable driver size or terminal differs')
lines = ['/* Generated from cable-driver.s; see the local connection documentation. */',
         'static const uint8_t cableDriver[] = {']
for offset in range(0, len(image), 16):
    lines.append('    ' + ', '.join(f'0x{value:02X}' for value in image[offset:offset + 16]) + ',')
lines.append('};')
expected = ('\n'.join(lines) + '\n').encode()
expected = FILE_HEADER.encode() + expected
target = Path(__file__).with_name('cable-driver.inc')
if args.write:
    target.write_bytes(expected)
elif target.read_bytes() != expected:
    raise SystemExit('Cable driver assembly differs from its embedded fixture')
print(f'Verified {len(image)} owned cable driver bytes')
