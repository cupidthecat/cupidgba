#!/usr/bin/env python3
"""Check the original CP14 timing, read values and flag transfer at actual issue."""

# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from fetch_inputs import download_verified


def word(data, address):
    offset = int(address, 16) - 0x08000000
    if offset < 0 or offset & 3 or offset + 4 > len(data):
        raise ValueError("CP14 observation lies outside the aligned cartridge image")
    return struct.unpack_from("<I", data, offset)[0]


def verify_input(data, case):
    if hashlib.sha256(data).hexdigest() != case["sha256"]:
        raise ValueError("CP14 cartridge checksum mismatch")
    checkpoint = int(case["checkpoint"], 16)
    for slot, expected in enumerate([0xE92D0003, 0xE3A00301, 0xE5901004, 0xE3110001]):
        if word(data, hex(checkpoint + slot * 4)) != expected:
            raise ValueError("CP14 result-display checkpoint mismatch")
    if word(data, case["terminal"]) != 0xEAFFFFFE:
        raise ValueError("CP14 terminal instruction mismatch")
    if len(case["observations"]) != 4:
        raise ValueError("CP14 requires all four original comparisons")
    for observation in case["observations"]:
        opcode = int(observation["instruction"], 16)
        expected = int(observation["value"], 16)
        if word(data, observation["address"]) != opcode:
            raise ValueError("CP14 observation instruction mismatch")
        if "mask" in observation:
            if (
                opcode >> 24 != 0x5A
                or int(observation["mask"], 16) != 0x80000000
                or expected != 0x80000000
                or word(data, observation["sample_address"]) & 0x80000000 != expected
            ):
                raise ValueError("CP14 flag guard changes the original N-bit expectation")
        elif "second_register" in observation:
            if (
                not 0 <= observation["register"] <= 15
                or not 0 <= observation["second_register"] <= 15
                or opcode
                != 0xE1500000 | observation["register"] << 16 | observation["second_register"]
                or word(data, observation["literal"]) != expected
                or word(data, observation["sample_address"]) != expected
            ):
                raise ValueError("CP14 read comparison changes the original operands")
        elif (
            not 0 <= observation["register"] <= 15
            or not 0 <= expected <= 255
            or opcode != 0xE3500000 | observation["register"] << 16 | expected
        ):
            raise ValueError("CP14 timing comparison changes the original immediate")


def observation_arguments(case):
    arguments = []
    for observation in case["observations"]:
        if "mask" in observation:
            arguments += [
                "--expect-status-at",
                observation["address"],
                observation["mask"],
                observation["value"],
            ]
        else:
            flag = (
                "--expect-register-pair-at"
                if "second_register" in observation
                else "--expect-register-at"
            )
            arguments += [
                flag,
                observation["address"],
                str(observation["register"]),
                observation["value"],
            ]
            if "second_register" in observation:
                arguments += [str(observation["second_register"]), observation["value"]]
    return arguments


def expected_lines(case):
    lines = []
    for index, observation in enumerate(case["observations"]):
        expected = int(observation["value"], 16)
        line = (
            f"observation={index} pc={int(observation['address'], 16):08x} mode=arm "
            f"opcode={int(observation['instruction'], 16):08x}"
        )
        if "mask" in observation:
            line += f" masked-cpsr={expected:08x} mask={int(observation['mask'], 16):08x} expected={expected:08x}"
        else:
            line += f" r{observation['register']}={expected:08x} expected={expected:08x}"
            if "second_register" in observation:
                line += f" r{observation['second_register']}={expected:08x} expected={expected:08x}"
        lines.append(line + " result=PASS")
    return lines


def require_result(output, returncode, case):
    lines = output.splitlines()
    boundaries = [
        "observations=4/4 result=PASS",
        "checkpoint=yes r12=0 result=PASS",
        "terminal=yes r12=0 result=PASS",
        f"terminal-issued=yes completed=yes pc={int(case['terminal'], 16):08x}",
    ]
    if (
        returncode
        or [line for line in lines if line.startswith("observation=")] != expected_lines(case)
        or any(lines.count(line) != 1 for line in boundaries)
        or any("result=FAIL" in line or "observation-order=FAIL" in line for line in lines)
    ):
        raise ValueError(
            "CP14 cartridge did not satisfy all original comparisons and completed boundaries"
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--bios", type=Path)
    parser.add_argument("--fetch", action="store_true")
    args = parser.parse_args()
    case = json.loads(Path(__file__).with_name("cp14-probe.json").read_text())
    rom = args.directory / case["path"]
    if args.fetch:
        base = "https://raw.githubusercontent.com/alyosha-tas/gba-tests/" + case["revision"]
        notice = download_verified(base + "/LICENSE", case["license_sha256"])
        data = download_verified(base + "/" + case["path"], case["sha256"])
        verify_input(data, case)
        if rom.exists() and rom.read_bytes() != data:
            raise SystemExit("Existing CP14 cartridge differs from the verified input")
        rom.parent.mkdir(parents=True, exist_ok=True)
        (args.directory / "cp14-LICENSE.txt").write_bytes(notice)
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
        *observation_arguments(case),
    ]
    if args.bios:
        command += ["--bios", str(args.bios.resolve())]
    result = subprocess.run(command, capture_output=True, text=True, timeout=90)
    print(result.stdout, end="")
    print(result.stderr, end="")
    require_result(result.stdout, result.returncode, case)
    print("PASS unchanged CP14 cartridge comparisons")


if __name__ == "__main__":
    main()
