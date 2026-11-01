#!/usr/bin/env python3
"""Require each original serial/HALT comparison and the completed result boundaries."""

# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from fetch_inputs import download_verified


def verify_input(data, case):
    if hashlib.sha256(data).hexdigest() != case["sha256"]:
        raise ValueError("Serial probe checksum mismatch")
    checkpoint = int(case["checkpoint"], 16) - 0x08000000
    signature = struct.pack("<4I", 0xE92D0003, 0xE3A00301, 0xE5901004, 0xE3110001)
    if checkpoint < 0 or data[checkpoint : checkpoint + len(signature)] != signature:
        raise ValueError("Serial checkpoint does not precede the original display routine")
    terminal = int(case["terminal"], 16) - 0x08000000
    if terminal < 0 or data[terminal : terminal + 4] != struct.pack("<I", 0xEAFFFFFE):
        raise ValueError("Serial terminal opcode mismatch")
    if not case["observations"]:
        raise ValueError("Serial probe has no comparisons")
    previous = -1
    for observation in case["observations"]:
        offset = int(observation["address"], 16) - 0x08000000
        opcode = int(observation["instruction"], 16)
        value = int(observation["value"], 16)
        register = observation["register"]
        if observation["mode"] != "thumb" or not 0 <= register <= 7 or not 0 <= value <= 255:
            raise ValueError("Unsupported serial comparison")
        if opcode != (0x2800 | register << 8 | value):
            raise ValueError("Serial comparison changes the original register or immediate")
        if (
            offset <= previous
            or offset & 1
            or offset >= checkpoint
            or data[offset : offset + 2] != struct.pack("<H", opcode)
        ):
            raise ValueError("Serial comparison opcode or order mismatch")
        previous = offset


def require_result(output, returncode, case):
    lines = output.splitlines()
    expected = []
    for index, observation in enumerate(case["observations"]):
        value = int(observation["value"], 16)
        expected.append(
            f"observation={index} pc={int(observation['address'], 16):08x} mode=thumb "
            f"opcode={int(observation['instruction'], 16):08x} "
            f"r{observation['register']}={value:08x} expected={value:08x} result=PASS"
        )
    count = len(expected)
    boundaries = [
        f"observations={count}/{count} result=PASS",
        "checkpoint=yes r12=0 result=PASS",
        "terminal=yes r12=0 result=PASS",
        f"terminal-issued=yes completed=yes pc={int(case['terminal'], 16):08x}",
    ]
    # Some failure paths copy the measured byte into the result register.
    # A wrong zero byte can therefore leave a passing-looking terminal.
    if (
        returncode
        or [line for line in lines if line.startswith("observation=")] != expected
        or any(lines.count(line) != 1 for line in boundaries)
        or any("result=FAIL" in line or "observation-order=FAIL" in line for line in lines)
    ):
        raise ValueError("Serial probe did not satisfy its comparisons and completed boundaries")


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
