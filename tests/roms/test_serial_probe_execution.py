"""Exercise zero-result false positives with owned instruction streams."""

# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile

from check_serial_probe import require_result, verify_input


def cartridge(value, skip):
    data = bytearray(0x180)

    def arm(offset, *words):
        struct.pack_into("<" + "I" * len(words), data, offset, *words)

    arm(0, 0xEA00002E)  # Reset entry branches over the unused cartridge header.
    # Initialize the result and enter the owned Thumb comparison stream.
    arm(0xC0, 0xE3A02000, 0xE59F0000, 0xE12FFF10, 0x08000101)
    thumb = [
        0xE004 if skip else 0x2700 | value,  # Skip the test or load the measurement.
        0x2F98,  # CMP r7, #0x98
        0xD101,  # BNE failure
        0x2200,  # Success records zero in r2.
        0xE000,  # B result
        0x463A,  # Failure copies r7 into r2, including a wrong zero.
        0x4800,  # Load the ARM result-entry address.
        0x4700,  # BX r0
    ]
    struct.pack_into("<8H", data, 0x100, *thumb)
    arm(0x110, 0x08000120)
    arm(0x120, 0xE1A0C002, 0xEA000005)
    # Retain the original display-entry signature without a display wait.
    arm(0x140, 0xE92D0003, 0xE3A00301, 0xE5901004, 0xE3110001, 0xE8BD0003, 0xEA000001)
    arm(0x160, 0xEAFFFFFE)
    case = dict(
        sha256=hashlib.sha256(data).hexdigest(),
        checkpoint="0x08000140",
        terminal="0x08000160",
        observations=[
            dict(address="0x08000102", instruction="0x2f98", mode="thumb", register=7, value="0x98")
        ],
    )
    return data, case


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cupid-serial-observer-") as directory:
        for name, value, skip, passing in [
            ("correct", 0x98, False, True),
            ("wrong-zero", 0, False, False),
            ("skipped-comparison", 0, True, False),
        ]:
            data, case = cartridge(value, skip)
            verify_input(data, case)
            path = Path(directory) / (name + ".gba")
            path.write_bytes(data)
            command = [
                str(args.runner.resolve()),
                str(path),
                "--frames",
                "3",
                "--steps",
                "100000",
                "--expect-loop-register",
                "12",
                "--checkpoint-address",
                case["checkpoint"],
                "--terminal-address",
                case["terminal"],
            ]
            unguarded = subprocess.run(command, text=True, capture_output=True, timeout=5)
            if unguarded.returncode or "terminal=yes r12=0 result=PASS" not in unguarded.stdout:
                raise SystemExit(f"Owned zero-result control failed: {name}\n{unguarded.stdout}")
            result = subprocess.run(
                command + ["--expect-register-at", "0x08000102", "7", "0x98"],
                text=True,
                capture_output=True,
                timeout=5,
            )
            accepted = True
            try:
                require_result(result.stdout, result.returncode, case)
            except ValueError:
                accepted = False
            if accepted != passing:
                raise SystemExit(f"Owned comparison classification failed: {name}\n{result.stdout}")
            print(f"PASS {name}: zero terminal accepted without observations; guarded={accepted}")


if __name__ == "__main__":
    main()
