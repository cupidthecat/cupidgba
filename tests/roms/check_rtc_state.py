#!/usr/bin/env python3
"""Observe the complete original result of a local RTC state corpus."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
from pathlib import Path
import re
import struct
import subprocess
import tempfile


def check_capture(data):
    if len(data) != 0x40000:
        raise SystemExit("Incomplete RTC state observer snapshot")
    size, = struct.unpack_from("<I", data)
    # A private printf transport records the original comparisons at EWRAM+4.
    # Its complete passing output includes all 17 results after the presence
    # and write guard. Missing, failed or partial output has a different digest.
    expected = "e18af96fa469e6931e7206fcfb504b63f5d4f9ce7f2ac3f4755bba67a3c4d116"
    if size != 495 or data[4 + size] != 0:
        raise SystemExit("Incomplete RTC state result text")
    if hashlib.sha256(data[4:4 + size]).hexdigest() != expected:
        raise SystemExit("RTC state corpus did not retain all original passing comparisons")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--bios", type=Path)
    args = parser.parse_args()
    expected_rom = "0f216044ff6ecb93d600b53ac84eab9fff53f00fa704e6b1de927061d6e82632"
    if hashlib.sha256(args.rom.read_bytes()).hexdigest() != expected_rom:
        raise SystemExit("RTC state corpus input checksum mismatch")
    # Retain the original corpus observation budgets and original wire source,
    # delays and comparisons. No cartridge or transport bytes are distributed.
    with tempfile.TemporaryDirectory(prefix="cupidgba-rtc-state-") as directory:
        snapshot = Path(directory) / "ewram.bin"
        command = [str(args.runner.resolve()), str(args.rom.resolve()),
                   "--frames", "360", "--steps", "150000000",
                   "--dump-ewram", str(snapshot)]
        if args.bios:
            command += ["--bios", str(args.bios.resolve())]
        result = subprocess.run(command, capture_output=True, text=True, timeout=90, check=True)
        print(result.stdout, end="")
        print(result.stderr, end="")
        data = snapshot.read_bytes()
    observation = re.search(r"^frames=(\d+) cycles=(\d+) steps=(\d+) pc=([0-9a-f]+)$",
                            result.stdout, re.MULTILINE)
    if not observation or int(observation[1]) != 360:
        raise SystemExit("RTC state corpus did not complete its observation interval")
    check_capture(data)
    print("PASS original RTC presence guard and all 17 state/status comparisons")


if __name__ == "__main__":
    main()
