#!/usr/bin/env python3
"""Check a locally built timer and IRQ suite with an external BIOS."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile


ROM_SHA256 = "92673c1e1c35fd149459c3382daa01cb4e2a01be1617ffa490e8c6b037f0d519"
TERMINAL = "0x08000310"
EXPECTED_RESULTS = (936, 936, 90, 90)


def verify_results(data):
    if len(data) != 0x40000:
        raise SystemExit("Incomplete EWRAM observer snapshot")
    results = struct.unpack_from("<4I", data)
    failures = struct.unpack_from("<I", data, 0x1000)[0]
    print(f"Timer count-up: {results[0]}/{results[1]}; IRQ: {results[2]}/{results[3]}")
    if results != EXPECTED_RESULTS or failures:
        raise SystemExit(f"Timer suite failed: results={results}, logged failures={failures}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--bios", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.rom.read_bytes()).hexdigest() != ROM_SHA256:
        raise SystemExit("Timer suite input checksum mismatch")
    if args.bios.stat().st_size != 0x4000:
        raise SystemExit("The external BIOS must be 16 KiB")
    with tempfile.TemporaryDirectory(prefix="cupidgba-timer-") as directory:
        snapshot = Path(directory) / "ewram.bin"
        command = [str(args.runner.resolve()), str(args.rom.resolve()),
                   "--bios", str(args.bios.resolve()), "--frames", "1000",
                   "--steps", "60000000", "--expect-loop-register", "12",
                   "--terminal-address", TERMINAL, "--dump-ewram", str(snapshot)]
        subprocess.run(command, check=True, timeout=90)
        verify_results(snapshot.read_bytes())
    print("PASS timer count-up and IRQ suite")


if __name__ == "__main__":
    main()
