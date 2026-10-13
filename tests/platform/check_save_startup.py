#!/usr/bin/env python3
"""Reject unsafe save input before the frontend starts emulation."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def check(frontend, rom, payload, flags=()):
    save = rom.with_suffix(".sav") if rom.suffix else Path(str(rom) + ".sav")
    save.write_bytes(payload)
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software")
    result = subprocess.run([str(frontend), str(rom), *flags], env=env,
                            capture_output=True, text=True, timeout=3)
    if result.returncode != 1 or "Cannot load save" not in result.stderr:
        raise SystemExit(f"Unsafe save input was not rejected: {result.stdout}\n{result.stderr}")
    if "CupidGBA started" in result.stdout or save.read_bytes() != payload:
        raise SystemExit("Failed startup ran emulation or changed the existing save")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frontend", type=Path, required=True)
    frontend = parser.parse_args().frontend.resolve()
    with tempfile.TemporaryDirectory(prefix="cupidgba-save-startup-") as directory:
        root = Path(directory)
        image = bytearray(512)
        struct.pack_into("<I", image, 0, 0xEA00002E)
        struct.pack_into("<I", image, 0xC0, 0xEAFFFFFE)
        image[0x180:0x188] = b"EEPROM_V"
        rom = root / "input.gba"
        rom.write_bytes(image)
        check(frontend, rom, bytes(range(256)) + b"x")
        check(frontend, rom, bytes(range(256)) * 32, ("-S", "eeprom512"))
        dotted = root / "folder.with.dots"
        dotted.mkdir()
        no_extension = dotted / "input"
        no_extension.write_bytes(image)
        check(frontend, no_extension, b"invalid save")
        too_long = str(root / ("x" * 520 + ".gba"))
        result = subprocess.run([str(frontend), too_long], capture_output=True, text=True, timeout=3)
        if result.returncode != 1 or "Save path is too long" not in result.stderr:
            raise SystemExit("An oversized save path was silently truncated")
    print("PASS save startup: invalid data, explicit size, dotted path and oversized path")


if __name__ == "__main__":
    main()
