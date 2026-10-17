#!/usr/bin/env python3
"""Verify or generate the complete owned local-cable driver image."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('image', type=Path)
parser.add_argument('--write', action='store_true')
args = parser.parse_args()
image = args.image.read_bytes()
if len(image) != 512 or any(image[offset:offset + 4] != bytes.fromhex('feffffea') for offset in (0xFC, 0x1FC)):
    raise SystemExit('Cable driver size or terminal differs')
lines = ['/* Generated from cable-driver.s; see the local connection documentation. */',
         'static const uint8_t cableDriver[] = {']
for offset in range(0, len(image), 12):
    lines.append('    ' + ', '.join(f'0x{value:02X}' for value in image[offset:offset + 12]) + ',')
lines.append('};')
expected = ('\n'.join(lines) + '\n').encode()
target = Path(__file__).with_name('cable-driver.inc')
if args.write:
    target.write_bytes(expected)
elif target.read_bytes() != expected:
    raise SystemExit('Cable driver assembly differs from its embedded fixture')
print(f'Verified {len(image)} owned cable driver bytes')
