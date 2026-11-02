#!/usr/bin/env python3
# @file check_bus_feedback.py
# @brief Require the original bus-input comparison and completed terminal of each pinned probe.
"""Require the original bus-input comparison and completed terminal of each pinned probe."""

# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from fetch_inputs import download_verified


def verify_input(data, case):
    if case["mode"] not in ("arm", "thumb"):
        raise ValueError("Unsupported bus comparison mode")
    if hashlib.sha256(data).hexdigest() != case["sha256"]:
        raise ValueError("Bus feedback probe checksum mismatch")
    offset = int(case["address"], 16) - 0x08000000
    opcode = int(case["opcode"], 16)
    value = int(case["expected"], 16)
    encoding = "<I" if case["mode"] == "arm" else "<H"
    if offset < 0 or data[offset : offset + struct.calcsize(encoding)] != struct.pack(
        encoding, opcode
    ):
        raise ValueError("Bus comparison opcode mismatch")
    if not 0 <= value <= 255 or opcode != (
        (0xE3520000 if case["mode"] == "arm" else 0x2A00) | value
    ):
        raise ValueError("Bus comparison expectation differs from encoded immediate")
    terminal = int(case["terminal"], 16) - 0x08000000
    if terminal < 0 or data[terminal : terminal + 4] != struct.pack("<I", 0xEAFFFFFE):
        raise ValueError("Bus terminal opcode mismatch")


def require_result(output, returncode, case):
    lines = output.splitlines()
    opcode = int(case["opcode"], 16)
    value = int(case["expected"], 16)
    expected = (
        f"observation=0 pc={int(case['address'], 16):08x} mode={case['mode']} "
        f"opcode={opcode:08x} r2={value:08x} expected={value:08x} result=PASS"
    )
    boundaries = [
        "observations=1/1 result=PASS",
        "terminal=yes r12=0 result=PASS",
        f"terminal-issued=yes completed=yes pc={int(case['terminal'], 16):08x}",
    ]
    # Require the live CMP operand. One original failure path leaves its
    # result unchanged; a zero terminal cannot prove that comparison passed.
    if (
        returncode
        or [line for line in lines if line.startswith("observation=")] != [expected]
        or any(lines.count(line) != 1 for line in boundaries)
        or any("result=FAIL" in line or "observation-order=FAIL" in line for line in lines)
    ):
        raise ValueError("Original bus comparison or completed terminal failed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", required=True, type=Path)
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--bios", type=Path)
    parser.add_argument("--test")
    parser.add_argument("--fetch", action="store_true")
    args = parser.parse_args()
    manifest = json.loads(Path(__file__).with_name("bus-feedback-probes.json").read_text())
    if args.fetch:
        base = "https://raw.githubusercontent.com/alyosha-tas/gba-tests/" + manifest["revision"]
        notice = download_verified(base + "/LICENSE", manifest["license_sha256"])
        args.directory.mkdir(parents=True, exist_ok=True)
        (args.directory / "bus-feedback-LICENSE.txt").write_bytes(notice)
        for case in manifest["cases"]:
            path = args.directory / case["path"]
            if path.exists():
                if hashlib.sha256(path.read_bytes()).hexdigest() != case["sha256"]:
                    raise SystemExit("Existing bus probe checksum mismatch")
                continue
            url = (
                "https://raw.githubusercontent.com/alyosha-tas/gba-tests/"
                + manifest["revision"]
                + "/"
                + case["path"]
            )
            data = download_verified(url, case["sha256"])
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        return
    if not args.runner or not args.test:
        parser.error("--runner and --test are required when not fetching")
    case = next((row for row in manifest["cases"] if row["name"] == args.test), None)
    if case is None:
        parser.error("Unknown bus feedback probe")
    path = args.directory / case["path"]
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != case["sha256"]:
        raise SystemExit("Bus feedback probe checksum mismatch")
    verify_input(data, case)
    command = [
        str(args.runner.resolve()),
        str(path.resolve()),
        "--frames",
        "360",
        "--steps",
        "150000000",
        "--expect-loop-register",
        "12",
        "--terminal-address",
        case["terminal"],
        "--expect-register-at",
        case["address"],
        "2",
        case["expected"],
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
    print(f"PASS original bus feedback probe: {case['name']}")


if __name__ == "__main__":
    main()
