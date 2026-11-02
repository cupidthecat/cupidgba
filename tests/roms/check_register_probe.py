#!/usr/bin/env python3
# @file check_register_probe.py
# @brief Verify original bank, stack, video-memory, timer and firmware comparisons.
"""Verify original bank, stack, video-memory, timer and firmware comparisons."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from fetch_inputs import download_verified
from protocols.comparisons import (
    observation_arguments, require_result, result_register, verify_input,
)


def fetch_cases(directory, manifest, cases):
    base = "https://raw.githubusercontent.com/alyosha-tas/gba-tests/" + manifest["revision"]
    inputs = [("register-probe-LICENSE.txt", "LICENSE", manifest["license_sha256"])]
    inputs += [(case["path"], case["path"], case["sha256"]) for case in cases]
    pending = []
    # Check the whole selection before creating files. Never replace an altered
    # cached input or permission notice, including a dangling symbolic link.
    for name, remote, digest in inputs:
        path = directory / name
        if not path.resolve().is_relative_to(directory.resolve()) or path.resolve() == directory.resolve():
            raise ValueError("Register input path leaves its directory")
        if path.exists() or path.is_symlink():
            if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
                raise ValueError("Existing register input differs from pinned data: " + name)
            if name != "register-probe-LICENSE.txt":
                verify_input(path.read_bytes(), next(case for case in cases if case["path"] == name))
        else:
            pending.append((name, remote, digest))
    verified = []
    for name, remote, digest in pending:
        data = download_verified(base + "/" + remote, digest)
        if name != "register-probe-LICENSE.txt":
            verify_input(data, next(case for case in cases if case["path"] == name))
        verified.append((directory / name, data, digest))
    # A transport or contract failure publishes none of the requested inputs.
    for path, data, digest in verified:
        path.parent.mkdir(parents=True, exist_ok=True)
        # Exclusive creation preserves an input published by another fetch.
        try:
            with path.open("xb") as output:
                output.write(data)
        except FileExistsError:
            if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
                raise ValueError("Concurrent register input differs from pinned data: " + name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true")
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--bios", type=Path)
    parser.add_argument("--test")
    args = parser.parse_args()
    manifest = json.loads(Path(__file__).with_name("register-probes.json").read_text())
    cases = manifest["cases"]
    if args.test:
        cases = [case for case in cases if case["name"] == args.test]
        if not cases:
            parser.error("Unknown register cartridge")
    if args.fetch:
        fetch_cases(args.directory, manifest, cases)
        return
    if not args.runner or not args.test:
        parser.error("--runner and --test are required unless fetching inputs")
    case = cases[0]
    if case["boot"] == "external" and not args.bios:
        parser.error("This cartridge measures the timer retained from external BIOS boot")
    rom = args.directory / case["path"]
    verify_input(rom.read_bytes(), case)
    command = [
        str(args.runner.resolve()), str(rom.resolve()),
        "--frames", "360", "--steps", "150000000",
        "--expect-loop-register", str(result_register(case)),
        "--checkpoint-address", case["checkpoint"], "--terminal-address", case["terminal"],
    ] + observation_arguments(case)
    if args.bios:
        command += ["--bios", str(args.bios.resolve())]
    result = subprocess.run(command, text=True, capture_output=True, timeout=90)
    print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="")
    require_result(result.stdout, result.returncode, case)
    print("PASS unchanged register cartridge: " + case["name"])


if __name__ == "__main__":
    main()
