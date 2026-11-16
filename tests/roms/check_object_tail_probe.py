#!/usr/bin/env python3
# @file      check_object_tail_probe.py
# @author    cupidthecat
# @brief     Require the original sprite texture-tail timer comparison and result boundaries.
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
"""Require the original sprite texture-tail timer comparison and result boundaries."""

# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import json
from pathlib import Path
import subprocess

from fetch_inputs import download_verified
from protocols.comparisons import require_result, verify_input


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--bios", type=Path)
    parser.add_argument("--fetch", action="store_true")
    args = parser.parse_args()
    manifest = json.loads(Path(__file__).with_name("object-tail-probe.json").read_text())
    case = manifest["case"]
    rom = args.directory / case["path"]
    if args.fetch:
        base = "https://raw.githubusercontent.com/alyosha-tas/gba-tests/" + manifest["revision"]
        notice = download_verified(base + "/LICENSE", manifest["license_sha256"])
        data = download_verified(base + "/" + case["path"], case["sha256"])
        verify_input(data, case)
        if rom.exists() and rom.read_bytes() != data:
            raise SystemExit("Existing object-tail cartridge differs from the verified input")
        rom.parent.mkdir(parents=True, exist_ok=True)
        (args.directory / "object-tail-LICENSE.txt").write_bytes(notice)
        rom.write_bytes(data)
        return
    if not args.runner:
        parser.error("Execution requires --runner")
    verify_input(rom.read_bytes(), case)
    command = [
        str(args.runner.resolve()),
        str(rom.resolve()),
        "--frames",
        "360",
        "--steps",
        "150000000",
        "--expect-loop-register",
        "12",
        "--checkpoint-address",
        case["checkpoint"],
        "--terminal-address",
        case["terminal"],
    ]
    for observation in case["observations"]:
        command += [
            "--expect-register-at",
            observation["address"],
            str(observation["register"]),
            observation["value"],
        ]
    if args.bios:
        command += ["--bios", str(args.bios.resolve())]
    result = subprocess.run(command, capture_output=True, text=True, timeout=90)
    print(result.stdout, end="")
    print(result.stderr, end="")
    require_result(result.stdout, result.returncode, case)
    print("PASS unchanged object texture-tail cartridge")


if __name__ == "__main__":
    main()
