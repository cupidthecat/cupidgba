#!/usr/bin/env python3
# @file      check_bus_suite.py
# @author    cupidthecat
# @brief     Require every observation in the pinned local bus timing suite to pass.
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
"""Require every observation in the pinned local bus timing suite to pass."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile


ROM_SHA256 = "577fc4ac3cef1c57bc11fcbb77699bbb23bad69d922cb8dfb4ef39668ca21362"
TERMINAL = "0x08000410"
EXPECTED_RESULTS = (2020, 2020)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--bios", type=Path)
    parser.add_argument("--runner", type=Path, required=True)
    args = parser.parse_args()
    if hashlib.sha256(args.rom.read_bytes()).hexdigest() != ROM_SHA256:
        raise SystemExit("Bus timing suite input checksum mismatch")
    if args.bios and args.bios.stat().st_size != 0x4000:
        raise SystemExit("The external BIOS must be 16 KiB")
    with tempfile.TemporaryDirectory(prefix="cupidgba-bus-") as directory:
        snapshot = Path(directory) / "ewram.bin"
        command = [str(args.runner.resolve()), str(args.rom.resolve()),
                   "--frames", "1000", "--steps", "60000000",
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
    print(f"Bus timing observations: {results[0]}/{results[1]}; logged failures: {failures}")
    if results != EXPECTED_RESULTS or failures:
        raise SystemExit("Bus timing suite failed")
    print("PASS complete bus timing suite")


if __name__ == "__main__":
    main()
