#!/usr/bin/env python3
"""Observe the original result of a local EEPROM polling software probe."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
from pathlib import Path
import re
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--bios", type=Path)
    args = parser.parse_args()
    expected_rom = "65fef2d1929a2f7ddaf1adc5d62badc48c5abb785bb84947dc719e8155b9a23a"
    if hashlib.sha256(args.rom.read_bytes()).hexdigest() != expected_rom:
        raise SystemExit("EEPROM probe input checksum mismatch")
    # The complete original corpus writes 1026 blocks, including a 1023-block
    # stress loop. Its estimated program time is about 111 million clocks.
    # This fixed budget also covers external startup, transfers and comparisons.
    with tempfile.TemporaryDirectory(prefix="cupidgba-eeprom-") as directory:
        vram, io = [Path(directory) / name for name in ("vram.bin", "io.bin")]
        command = [str(args.runner.resolve()), str(args.rom.resolve()),
                   "--frames", "1200", "--steps", "150000000",
                   "--dump-vram", str(vram), "--dump-io", str(io)]
        if args.bios:
            command += ["--bios", str(args.bios.resolve())]
        result = subprocess.run(command, capture_output=True, text=True, timeout=90, check=True)
        print(result.stdout, end="")
        print(result.stderr, end="")
        video, registers = vram.read_bytes(), io.read_bytes()
    if len(video) != 0x18000 or len(registers) != 0x400:
        raise SystemExit("Incomplete EEPROM observer snapshots")
    observation = re.search(r"^frames=(\d+) cycles=(\d+) steps=(\d+) pc=([0-9a-f]+)$",
                            result.stdout, re.MULTILINE)
    # Both result paths return to this unique ARM self-branch. R12 retains
    # a stress-loop marker on success, so it is not a zero-valued result flag.
    if not observation or int(observation[1]) != 1200 or int(observation[4], 16) != 0x08000844:
        raise SystemExit("EEPROM probe did not reach its original terminal loop")
    if struct.unpack_from("<H", registers)[0] != 0x0404:
        raise SystemExit("EEPROM probe did not retain its original result display mode")
    # Check every pixel in the eight full-width result rows. The expected
    # region renders 'Passed all tests'; blank, partial and failure text differ.
    expected_pixels = "c49baf822bfef6667f95105036611e8516e4a2a66e854f7a9db54bf0a1d72483"
    if hashlib.sha256(video[70 * 240:78 * 240]).hexdigest() != expected_pixels:
        raise SystemExit("EEPROM probe did not display its complete passing result")
    print("PASS original EEPROM polling and complete stress-loop comparisons")


if __name__ == "__main__":
    main()
