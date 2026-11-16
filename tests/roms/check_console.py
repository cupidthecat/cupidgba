#!/usr/bin/env python3
# @file      check_console.py
# @author    cupidthecat
# @brief     Check the visible result text of checksum-verified hardware test ROMs.
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
"""Check the visible result text of checksum-verified hardware test ROMs."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from fetch_inputs import download_verified


def console_text(vram, io):
    if len(vram) != 0x18000 or len(io) != 0x400:
        raise SystemExit("Incomplete observer snapshots")
    # The unmodified test console uses BG0 and ASCII glyph indices.
    bg0 = struct.unpack_from("<H", io, 8)[0]
    screenbase = ((bg0 >> 8) & 31) * 2048
    lines = []
    for row in range(20):
        tiles = struct.unpack_from("<32H", vram, screenbase + row * 64)
        lines.append("".join(chr(tile & 1023) if 32 <= (tile & 1023) < 127
                             else " " for tile in tiles).rstrip())
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true")
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--bios", type=Path)
    parser.add_argument("--test")
    args = parser.parse_args()
    manifest = json.loads(Path(__file__).with_name("console-manifest.json").read_text())
    cases = [case for case in manifest["roms"] if not args.test or case["name"] == args.test]
    if not cases:
        parser.error("No matching ROM test")
    if not args.fetch and not args.runner:
        parser.error("--runner is required unless fetching inputs")
    for case in cases:
        path = args.directory / "hardware" / case["path"]
        if args.fetch:
            url = (f'{manifest["repository"]}/raw/commit/{manifest["revision"]}/'
                   f'{case["path"]}')
            data = download_verified(url, case["sha256"])
        else:
            data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != case["sha256"]:
            raise SystemExit(f'Input checksum mismatch: {case["name"]}')
        if args.fetch:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            print(f'Fetched verified input: {case["name"]}')
            continue
        with tempfile.TemporaryDirectory(prefix="cupidgba-rom-") as directory:
            vram, io = [Path(directory) / name for name in ("vram.bin", "io.bin")]
            # An external BIOS executes its startup animation before the test.
            frames = case.get("external_frames", 400) if args.bios else case.get("frames", 20)
            command = [str(args.runner.resolve()), str(path.resolve()), "--frames", str(frames),
                       "--dump-vram", str(vram), "--dump-io", str(io)]
            if args.bios:
                command += ["--bios", str(args.bios.resolve())]
            subprocess.run(command, timeout=45, check=True)
            actual = console_text(vram.read_bytes(), io.read_bytes())
        print(actual)
        missing = [line for line in case["lines"] if line not in actual.splitlines()]
        if missing or "FAIL" in actual:
            raise SystemExit(f'{case["name"]}: missing expected results: {missing}')
        print(f'PASS {case["name"]}')


if __name__ == "__main__":
    main()
