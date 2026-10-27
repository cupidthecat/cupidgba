#!/usr/bin/env python3
"""Require the original empty POP, timer/SP comparisons and issued result terminal."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from fetch_inputs import download_verified


def store(path, data, digest):
    if path.exists():
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError("Existing Thumb stack input differs from its pinned data")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as output:
        output.write(data)


def verify_input(data, case):
    if len(data) != case["bytes"] or hashlib.sha256(data).hexdigest() != case["sha256"]:
        raise ValueError("Thumb stack cartridge checksum or length mismatch")
    for observation in case["observations"]:
        width = 2 if observation["mode"] == "thumb" else 4
        if observation["mode"] not in ("arm", "thumb"):
            raise ValueError("Unknown Thumb stack instruction mode")
        offset = int(observation["address"], 16) - 0x08000000
        expected = int(observation["instruction"], 16).to_bytes(width, "little")
        if offset < 0 or data[offset:offset + width] != expected:
            raise ValueError("Thumb stack observation changes an original instruction")
    # Preserve the original timer control, literal return target and PUSH/POP
    # setup. The cartridge's timer and SP comparisons remain unchanged.
    if data[0x150:0x166] != struct.pack("<3I5H", 0x4014, 0x00800000, 0x08000168,
                                               0x6820, 0x6018, 0xB402, 0x1C00, 0xBC00):
        raise ValueError("Thumb stack timer or caller setup changed")
    if data[0x176:0x182] != struct.pack("<6H", 0x2816, 0xD105, 0x4668, 0x4038, 0x283C, 0xD104):
        raise ValueError("Thumb stack expected comparisons changed")
    checkpoint = int(case["checkpoint"], 16) - 0x08000000
    if data[checkpoint:checkpoint + 16] != struct.pack("<4I", 0xE92D0003, 0xE3A00301, 0xE5901004, 0xE3110001):
        raise ValueError("Thumb stack checkpoint is not the original display entry")
    terminal = int(case["terminal"], 16) - 0x08000000
    if data[terminal:terminal + 4] != struct.pack("<I", 0xEAFFFFFE):
        raise ValueError("Thumb stack terminal is not the original idle branch")


def require_result(stdout, status, case):
    lines = stdout.splitlines()
    expected = [
        f"observation={index} pc={int(row['address'], 16):08x} mode={row['mode']} "
        f"opcode={int(row['instruction'], 16):08x} r{row['register']}={row['value']:08x} "
        f"expected={row['value']:08x} result=PASS"
        for index, row in enumerate(case["observations"])
    ]
    actual = [line for line in lines if line.startswith("observation=")]
    count = len(expected)
    required = ["terminal=yes r12=0 result=PASS", "checkpoint=yes r12=0 result=PASS",
                f"observations={count}/{count} result=PASS"]
    if status or actual != expected or any(lines.count(line) != 1 for line in required) or \
            any("result=FAIL" in line or "observation-order=FAIL" in line for line in lines):
        raise ValueError("Thumb stack cartridge did not satisfy every original result boundary")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true")
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--bios", type=Path)
    args = parser.parse_args()
    manifest = json.loads(Path(__file__).with_name("thumb-stack-probes.json").read_text())
    if args.fetch:
        source = f'https://raw.githubusercontent.com/jsmolka/gba-tests/{manifest["revision"]}'
        digest = manifest["license_sha256"]
        store(args.directory / "thumb-stack-LICENSE.txt", download_verified(source + "/LICENSE", digest), digest)
        for case in manifest["cases"]:
            data = download_verified(source + "/" + case["path"], case["sha256"])
            verify_input(data, case)
            store(args.directory / case["path"], data, case["sha256"])
            print("Fetched verified Thumb stack input: " + case["name"])
        return
    if not args.runner:
        parser.error("--runner is required unless fetching inputs")
    for case in manifest["cases"]:
        rom = args.directory / case["path"]
        verify_input(rom.read_bytes(), case)
        command = [str(args.runner.resolve()), str(rom.resolve()), "--frames", "360", "--steps", "150000000",
                   "--expect-loop-register", "12", "--checkpoint-address", case["checkpoint"],
                   "--terminal-address", case["terminal"]]
        for row in case["observations"]:
            command += ["--expect-register-at", row["address"], str(row["register"]), str(row["value"])]
        if args.bios:
            command += ["--bios", str(args.bios.resolve())]
        result = subprocess.run(command, capture_output=True, text=True, timeout=90)
        print(result.stdout, end="")
        if result.stderr:
            print(result.stderr, end="")
        require_result(result.stdout, result.returncode, case)
        print("PASS unchanged Thumb stack cartridge: " + case["name"])


if __name__ == "__main__":
    main()
