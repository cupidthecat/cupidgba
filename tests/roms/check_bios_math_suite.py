#!/usr/bin/env python3
# @file      check_bios_math_suite.py
# @author    cupidthecat
# @brief     Require all register observations from the pinned local BIOS math suite.
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
"""Require all register observations from the pinned local BIOS math suite."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile


ROM_SHA256 = "2c95d2b1eba34c5af6fdf96d0270651e22593714c570fc6a7ec477c9c9f611cb"
TERMINAL = "0x080002d4"
EXPECTED_RESULTS = (615, 615)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--bios", type=Path)
    parser.add_argument("--runner", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.rom.read_bytes()).hexdigest() != ROM_SHA256:
        raise SystemExit("BIOS math suite input checksum mismatch")
    if args.bios and args.bios.stat().st_size != 0x4000:
        raise SystemExit("The external BIOS must be 16 KiB")
    with tempfile.TemporaryDirectory(prefix="cupidgba-math-") as directory:
        snapshot = Path(directory) / "ewram.bin"
        command = [str(args.runner.resolve()), str(args.rom.resolve()),
                   "--frames", "300", "--steps", "40000000",
                   "--expect-loop-register", "12", "--terminal-address", TERMINAL,
                   "--dump-ewram", str(snapshot)]
        if args.bios:
            command.extend(["--bios", str(args.bios.resolve())])
        subprocess.run(command, check=True, timeout=90)
        data = snapshot.read_bytes()
    if len(data) != 0x40000:
        raise SystemExit("Incomplete EWRAM observer snapshot")
    results = struct.unpack_from("<2I", data)
    failures = struct.unpack_from("<I", data, 0x10000)[0]
    print(f"BIOS math observations: {results[0]}/{results[1]}; logged failures: {failures}")
    if results != EXPECTED_RESULTS or failures:
        raise SystemExit("BIOS math suite failed")
    print("PASS complete BIOS math suite")


if __name__ == "__main__":
    main()
