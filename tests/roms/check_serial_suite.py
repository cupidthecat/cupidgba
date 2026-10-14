#!/usr/bin/env python3
"""Require all counted serial timing and mode-specific register observations."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile


ROM_SHA256 = "d4e873a64d05d0d9640dd0ba7463549bfb58005b0f0aeef76c417a1493405b45"
TIMING_CYCLES = (0x7D61, 0x2133, 0x16EF, 0xCBD, 0x279, 0xB9, 0x879, 0x179)
READ_ADDRESSES = (0x120, 0x122, 0x124, 0x126, 0x128, 0x12A, 0x134,
                  0x136, 0x140, 0x142, 0x150, 0x152, 0x154, 0x156, 0x15A)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--bios", type=Path)
    parser.add_argument("--runner", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.rom.read_bytes()).hexdigest() != ROM_SHA256:
        raise SystemExit("Serial suite input checksum mismatch")
    with tempfile.TemporaryDirectory(prefix="cupidgba-serial-") as directory:
        snapshot = Path(directory) / "ewram.bin"
        command = [str(args.runner.resolve()), str(args.rom.resolve()),
                   "--frames", "360", "--expect-loop-register", "12",
                   "--terminal-address", "0x0800031c", "--dump-ewram", str(snapshot)]
        if args.bios:
            command.extend(["--bios", str(args.bios.resolve())])
        subprocess.run(command, check=True, timeout=90)
        data = snapshot.read_bytes()
    if len(data) != 0x40000:
        raise SystemExit("Incomplete EWRAM observer snapshot")
    counts = struct.unpack_from("<4I", data)
    failures = struct.unpack_from("<2I", data, 0x10000)
    if counts != (4, 4, 90, 90) or failures != (0, 0):
        raise SystemExit(f"Serial counts {counts}, failure logs {failures}")
    if struct.unpack_from("<I", data, 0x11000)[0] != 8:
        raise SystemExit("Incomplete serial timing observations")
    for case, expected in enumerate(TIMING_CYCLES):
        actual, timeout, recorded, expected_timeout, passed = struct.unpack_from(
            "<5I", data, 0x11004 + 20 * case)
        skipped = case < 4
        wanted = (0xFFFFFFFF if skipped else expected, int(skipped), expected,
                  int(skipped), int(not skipped))
        if (actual, timeout, recorded, expected_timeout, passed) != wanted:
            raise SystemExit(f"Serial timing case {case} failed or changed its original skip")
    if struct.unpack_from("<I", data, 0x12000)[0] != 90:
        raise SystemExit("Incomplete serial register observations")
    for case in range(90):
        address, control, expected, passed = struct.unpack_from("<4I", data, 0x12004 + 16 * case)
        wanted_address = 0x04000000 + READ_ADDRESSES[case % 15]
        wanted_control = (0x2000, 0, 0x1000, 0x3000, 0, 0)[case // 15]
        if address != wanted_address or control != wanted_control or passed != 1:
            raise SystemExit(f"Serial register case {case}, address {address:#x}, expected {expected:#x} failed")
    print("PASS serial suite: 4/4 counted timing, 90/90 registers, 4 original multiplayer skips")


if __name__ == "__main__":
    main()
