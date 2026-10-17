#!/usr/bin/env python3
"""Observe all original user-bank failure guards and result boundaries."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    cases = json.loads(Path(__file__).with_name("user-bank-probes.json").read_text())["cases"]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--bios", type=Path)
    parser.add_argument("--test", choices=[case["name"] for case in cases], required=True)
    args = parser.parse_args()
    case = next(case for case in cases if case["name"] == args.test)
    rom = args.directory / case["path"]
    if hashlib.sha256(rom.read_bytes()).hexdigest() != case["sha256"]:
        raise SystemExit("User-bank probe checksum mismatch")
    command = [str(args.runner.resolve()), "probe", str(rom.resolve()),
               str(args.bios.resolve()) if args.bios else "-", case["checkpoint"],
               str(case["result_register"]), case["terminal"]]
    for guard in case["guards"]:
        command.extend([guard["address"], guard["instruction"], str(guard["zero"])])
    result = subprocess.run(command, capture_output=True, text=True, timeout=90)
    print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="")
    count = len(case["guards"])
    if result.returncode or not re.search(
        rf"^checkpoint=yes guards={count}/{count} terminal=yes r0=0 result=PASS$", result.stdout, re.MULTILINE
    ):
        raise SystemExit("User-bank probe failed an original guard or result boundary")
    print(f"PASS unchanged user-bank probe: {case['name']}, {count} original failure guards")


if __name__ == "__main__":
    main()
