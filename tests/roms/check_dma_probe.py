#!/usr/bin/env python3
# @file check_dma_probe.py
# @brief Require every original DMA comparison and both completed result boundaries.
"""Require every original DMA comparison and both completed result boundaries."""

# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import json
from pathlib import Path
import subprocess
from protocols.comparisons import observation_arguments, require_result, verify_input


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--bios", type=Path)
    parser.add_argument("--test", required=True)
    args = parser.parse_args()
    cases = json.loads(Path(__file__).with_name("dma-probes.json").read_text())["cases"]
    case = next((case for case in cases if case["name"] == args.test), None)
    if case is None:
        parser.error("Unknown DMA cartridge")
    rom = args.directory / case["path"]
    try:
        verify_input(rom.read_bytes(), case)
    except ValueError as error:
        raise SystemExit(str(error)) from error
    command = [
        str(args.runner.resolve()),
        str(rom.resolve()),
        "--frames",
        "360",
        "--steps",
        "150000000",
        "--expect-loop-register",
        "12",
        "--terminal-address",
        case["terminal"],
        "--checkpoint-address",
        case["checkpoint"],
    ]
    command += observation_arguments(case)
    if args.bios:
        command += ["--bios", str(args.bios.resolve())]
    result = subprocess.run(command, text=True, capture_output=True, timeout=90)
    print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="")
    try:
        require_result(result.stdout, result.returncode, case)
    except ValueError as error:
        raise SystemExit(str(error)) from error
    print(f"PASS unchanged DMA cartridge: {args.test}")


if __name__ == "__main__":
    main()
