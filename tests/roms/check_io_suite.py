#!/usr/bin/env python3
"""Require every observation in the pinned local I/O register-read suite."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile


ROM_SHA256 = "561aaf12f7cf5a64e53ebc40eec8f363b043e7f983fa79f6b1b2205c2aeb5f44"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--bios", type=Path)
    parser.add_argument("--runner", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.rom.read_bytes()).hexdigest() != ROM_SHA256:
        raise SystemExit("I/O suite input checksum mismatch")
    with tempfile.TemporaryDirectory(prefix="cupidgba-io-") as directory:
        snapshot = Path(directory) / "ewram.bin"
        command = [str(args.runner.resolve()), str(args.rom.resolve()),
                   "--frames", "360", "--expect-loop-register", "12",
                   "--terminal-address", "0x080002c8", "--dump-ewram", str(snapshot)]
        if args.bios:
            command.extend(["--bios", str(args.bios.resolve())])
        subprocess.run(command, check=True, timeout=90)
        data = snapshot.read_bytes()
    if len(data) != 0x40000:
        raise SystemExit("Incomplete EWRAM observer snapshot")
    passes, total = struct.unpack_from("<2I", data)
    failures = struct.unpack_from("<I", data, 0x10000)[0]
    cases = struct.unpack_from("<I", data, 0x11000)[0]
    if (passes, total, failures, cases) != (130, 130, 0, 130):
        raise SystemExit(f"I/O suite: {passes}/{total}; {failures} failures; {cases} cases")
    for case in range(cases):
        address, expected, passed = struct.unpack_from("<3I", data, 0x11004 + 12 * case)
        if passed != 1:
            raise SystemExit(f"I/O case {case}, address {address:#x}, expected {expected:#x} failed")
    print("PASS complete I/O register-read suite: 130/130")


if __name__ == "__main__":
    main()
