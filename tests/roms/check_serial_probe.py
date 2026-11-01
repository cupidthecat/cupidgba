#!/usr/bin/env python3
"""Require each original serial/HALT comparison and the completed result boundaries."""

# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from fetch_inputs import download_verified
from thumb_comparisons import require_result, verify_input


def fetch_cases(manifest, cases, directory):
    base = "https://raw.githubusercontent.com/alyosha-tas/gba-tests/" + manifest["revision"]
    notice = download_verified(base + "/LICENSE", manifest["license_sha256"])
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "serial-probes-LICENSE.txt").write_bytes(notice)
    for case in cases:
        path = directory / case["path"]
        if path.exists():
            if hashlib.sha256(path.read_bytes()).hexdigest() != case["sha256"]:
                raise ValueError("Existing serial probe checksum mismatch")
            continue
        data = download_verified(base + "/" + case["source_path"], case["sha256"])
        path.write_bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--bios", type=Path)
    manifest = json.loads(Path(__file__).with_name("serial-probes.json").read_text())
    cases = manifest["cases"]
    parser.add_argument("--test", choices=[case["name"] for case in cases])
    parser.add_argument("--fetch", action="store_true")
    args = parser.parse_args()
    if args.fetch:
        selected = [case for case in cases if not args.test or case["name"] == args.test]
        fetch_cases(manifest, selected, args.directory)
        return
    if not args.runner or not args.test:
        parser.error("Execution requires --runner and --test")
    case = next(case for case in cases if case["name"] == args.test)
    if case["requires_bios"] and not args.bios:
        parser.error("HALT return-address probes require --bios for original firmware addresses")
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
    try:
        require_result(result.stdout, result.returncode, case)
    except ValueError as error:
        raise SystemExit(str(error)) from error
    print(f"PASS unchanged serial probe: {args.test}")


if __name__ == "__main__":
    main()
