"""Exercise ARM/Thumb zero-result false positives with owned instruction streams."""

# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile

from protocols.comparisons import require_result, verify_input


def cartridge(value, skip, mode, result_register):
    data = bytearray(0x1A0)

    def arm(offset, *words):
        struct.pack_into("<" + "I" * len(words), data, offset, *words)

    arm(0, 0xEA00002E)  # Reset entry branches over the unused cartridge header.
    if mode == "thumb":
        # Initialize the result and enter the owned Thumb comparison stream.
        arm(0xC0, 0xE3A02000, 0xE59F0000, 0xE12FFF10, 0x08000101)
        narrow = [
            0xE004 if skip else 0x2700 | value,  # Skip the test or load the measurement.
            0x2F98,  # CMP r7, #0x98
            0xD101,  # BNE failure
            0x2200,  # Success records zero in r2.
            0xE000,  # B result
            0x463A,  # Failure copies r7 into r2, including a wrong zero.
            0x4800,  # Load the ARM result-entry address.
            0x4700,  # BX r0
        ]
        struct.pack_into("<8H", data, 0x100, *narrow)
        arm(0x110, 0x08000120)
        comparison, opcode = "0x08000102", "0x2f98"
    else:
        arm(0xC0, 0xE3A02000, 0xEA00000D)
        arm(
            0x100,
            0xEA000004 if skip else 0xE3A07000 | value,
            0xE3570098,  # CMP r7, #0x98
            0x1A000001,  # BNE failure
            0xE3A02000,  # Success records zero in r2.
            0xEA000000,  # B result
            0xE1A02007,  # Failure copies r7 into r2, including a wrong zero.
            0xEA000000,  # B result entry
        )
        comparison, opcode = "0x08000104", "0xe3570098"
    arm(0x120, 0xE1A00002 | result_register << 12, 0xEA000005)
    # Execute the complete VBlank entry and preserve the selected result register.
    arm(
        0x140,
        0xE92D0003,
        0xE3A00301,
        0xE5901004,
        0xE3110001,
        0x1AFFFFFC,
        0xE5901004,
        0xE3110001,
        0x0AFFFFFC,
        0xE8BD0003,
        0xE92D1FFF,
        0xE1B0C000 | result_register,
        0xEA000002,
    )
    arm(0x17C, 0xE8BD1FFF, 0xEAFFFFFE)
    case = dict(
        sha256=hashlib.sha256(data).hexdigest(),
        checkpoint="0x08000140",
        terminal="0x08000180",
        result_register=result_register,
        observations=[
            dict(address=comparison, instruction=opcode, mode=mode, register=7, value="0x98")
        ],
    )
    return data, case


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cupid-immediate-observer-") as directory:
        for mode in ["thumb", "arm"]:
            for result_register in [12, 7]:
                for name, value, skip, passing in [
                    ("correct", 0x98, False, True),
                    ("wrong-zero", 0, False, False),
                    ("skipped-comparison", 0, True, False),
                ]:
                    label = f"{mode}-r{result_register}-{name}"
                    data, case = cartridge(value, skip, mode, result_register)
                    verify_input(data, case)
                    path = Path(directory) / (label + ".gba")
                    path.write_bytes(data)
                    command = [
                        str(args.runner.resolve()),
                        str(path),
                        "--frames",
                        "3",
                        "--steps",
                        "100000",
                        "--expect-loop-register",
                        str(result_register),
                        "--checkpoint-address",
                        case["checkpoint"],
                        "--terminal-address",
                        case["terminal"],
                    ]
                    unguarded = subprocess.run(command, text=True, capture_output=True, timeout=5)
                    if (
                        unguarded.returncode
                        or f"terminal=yes r{result_register}=0 result=PASS" not in unguarded.stdout
                    ):
                        raise SystemExit(
                            f"Owned zero-result control failed: {label}\n{unguarded.stdout}"
                        )
                    result = subprocess.run(
                        command
                        + ["--expect-register-at", case["observations"][0]["address"], "7", "0x98"],
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
                        raise SystemExit(
                            f"Owned comparison classification failed: {label}\n{result.stdout}"
                        )
                    print(
                        f"PASS {label}: zero terminal accepted without observations; guarded={accepted}"
                    )


if __name__ == "__main__":
    main()
