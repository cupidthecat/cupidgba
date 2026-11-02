#!/usr/bin/env python3
# @file check_cyclic_display.py
# @brief Check repeated original OBJ control stores; rendered pixels remain unqualified.
"""Check repeated original OBJ control stores; rendered pixels remain unqualified."""

# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import json
from pathlib import Path
import subprocess

from check_display_probe import fetch_cases, input_path
from protocols.cycles import observation_arguments, require_result, verify_input


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--bios", type=Path)
    parser.add_argument("--screenshot", type=Path)
    parser.add_argument("--fetch", action="store_true")
    args = parser.parse_args()
    manifest = json.loads(Path(__file__).with_name("cyclic-display-probes.json").read_text())
    if args.fetch:
        fetch_cases(manifest, manifest["cases"], args.directory, verify=verify_input)
        return
    if not args.runner:
        parser.error("Execution requires --runner")
    for case in manifest["cases"]:
        rom = input_path(args.directory, case["path"])
        verify_input(rom.read_bytes(), case)
        command = [str(args.runner.resolve()), str(rom), "--frames", str(case["frames"]),
                   "--steps", str(case["steps"]), *observation_arguments(case)]
        if args.bios:
            command += ["--bios", str(args.bios.resolve())]
        if args.screenshot:
            command += ["--screenshot", str(args.screenshot.resolve())]
        result = subprocess.run(command, capture_output=True, text=True, timeout=90)
        print(result.stdout, end="")
        print(result.stderr, end="")
        cycles, partial = require_result(result.stdout, result.returncode, case)
        print(f"PASS original display control sequence: {cycles} cycles, {partial} partial stores; "
              "rendered pixels unqualified")


if __name__ == "__main__":
    main()
