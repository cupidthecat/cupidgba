#!/usr/bin/env python3
"""Generate or verify the embedded image of the owned JOY driver."""
from pathlib import Path
import argparse

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("image", type=Path)
parser.add_argument("--write", action="store_true")
args = parser.parse_args()
image = args.image.read_bytes()
if len(image) != 512 or image[0xFC:0x100] != bytes.fromhex("feffffea") or image[0x1FC:0x1FE] != bytes.fromhex("fee7"):
    raise SystemExit("JOY driver size or terminal differs")
rows = ["/* Generated from joybus-driver.s; see the serial documentation. */",
        "static const uint8_t joybusDriver[] = {"]
for offset in range(0, len(image), 12):
    rows.append("    " + ", ".join(f"0x{value:02X}" for value in image[offset:offset + 12]) + ",")
rows.append("};")
expected = ("\n".join(rows) + "\n").encode()
target = Path(__file__).with_name("joybus-driver.inc")
if args.write:
    target.write_bytes(expected)
elif target.read_bytes() != expected:
    raise SystemExit("JOY driver assembly output differs from the embedded fixture")
print(f"Verified {len(image)} owned JOY driver bytes")
