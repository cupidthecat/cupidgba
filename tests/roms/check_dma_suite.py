#!/usr/bin/env python3
"""Check the transfer results of a locally built DMA suite with either BIOS path."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile


ROM_SHA256 = "4b2b7c0ade67736382db14bb38b04751b04cc6b24f97d4febea332c6f2351231"
TERMINAL = "0x080003c0"
EXPECTED_RESULTS = (1244, 1244)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--bios", type=Path)
    parser.add_argument("--runner", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.rom.read_bytes()).hexdigest() != ROM_SHA256:
        raise SystemExit("DMA suite input checksum mismatch")
    if args.bios and args.bios.stat().st_size != 0x4000:
        raise SystemExit("The external BIOS must be 16 KiB")
    with tempfile.TemporaryDirectory(prefix="cupidgba-dma-") as directory:
        snapshot = Path(directory) / "ewram.bin"
        command = [str(args.runner.resolve()), str(args.rom.resolve()),
                   "--frames", "1000", "--steps", "60000000",
                   "--expect-loop-register", "12", "--terminal-address", TERMINAL,
                   "--dump-ewram", str(snapshot)]
        if args.bios:
            command += ["--bios", str(args.bios.resolve())]
        subprocess.run(command, check=True, timeout=90)
        data = snapshot.read_bytes()
    if len(data) != 0x40000:
        raise SystemExit("Incomplete EWRAM observer snapshot")
    results = struct.unpack_from("<2I", data)
    failures = struct.unpack_from("<I", data, 0x10000)[0]
    print(f"DMA transfer results: {results[0]}/{results[1]}; logged failures: {failures}")
    if results != EXPECTED_RESULTS or failures:
        raise SystemExit("DMA transfer suite failed")
    print("PASS DMA transfer suite")


if __name__ == "__main__":
    main()
