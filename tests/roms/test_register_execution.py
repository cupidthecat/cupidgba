# @file test_register_execution.py
# @brief Execute owned literal comparisons that deliberately hide failures in zero results.
"""Execute owned literal comparisons that deliberately hide failures in zero results."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import contextlib
import hashlib
import io
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from unittest.mock import patch

import check_register_probe as probe
from protocols.comparisons import verify_input
from test_immediate_execution import cartridge


def literal_cartridge(measured, expected, skip, register):
    data, case = cartridge(0x98, False, "arm", register)
    struct.pack_into(
        "<7I", data, 0x100,
        0xEA000003 if skip else 0xE59F0030,  # Skip the comparison or load the measured word.
        0xE28F2030,  # ADD r2, PC, #0x30: form the expected literal address.
        0xE5922000,  # LDR r2, [r2]: load the original expected operand.
        0xE1A00000 if expected else 0xE3A02000,
        0xE1500002,  # CMP r0, r2
        0xE3A00000 | register << 12,  # Hide the comparison result behind a zero.
        0xEA000008,  # Branch to the complete result entry.
    )
    struct.pack_into("<2I", data, 0x138, measured, 0x55550000)
    case["boot"] = "either"
    case["observations"] = [dict(
        address="0x08000110", instruction="0xe1500002", mode="arm",
        register=0, second_register=2, value="0x55550000",
        literal="0x0800013c", literal_load="0x08000104",
    )]
    case["sha256"] = hashlib.sha256(data).hexdigest()
    return data, case


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cupid-register-observer-") as temporary:
        for register in (0, 12):
            for name, measured, expected, skip, passing in (
                ("correct", 0x55550000, True, False, True),
                ("wrong-zero", 0, True, False, False),
                ("wrong-expected", 0x55550000, False, False, False),
                ("equal-but-wrong", 0, False, False, False),
                ("skipped", 0x55550000, True, True, False),
            ):
                data, case = literal_cartridge(measured, expected, skip, register)
                case.update(name=f"r{register}-{name}", path=f"r{register}-{name}.gba")
                verify_input(data, case)
                path = Path(temporary) / case["path"]
                path.write_bytes(data)
                unguarded = subprocess.run([
                    str(args.runner.resolve()), str(path), "--frames", "3", "--steps", "100000",
                    "--expect-loop-register", str(register),
                    "--checkpoint-address", case["checkpoint"], "--terminal-address", case["terminal"],
                ], capture_output=True, text=True, timeout=5)
                if unguarded.returncode or f"terminal=yes r{register}=0 result=PASS" not in unguarded.stdout:
                    raise SystemExit("Owned zero-result control failed: " + case["name"])
                argv = ["check_register_probe.py", "--directory", temporary,
                        "--runner", str(args.runner.resolve()), "--test", case["name"]]
                accepted = True
                output = io.StringIO()
                with patch.object(sys, "argv", argv), patch.object(
                    probe.json, "loads", return_value={"cases": [case]}
                ), contextlib.redirect_stdout(output):
                    try:
                        probe.main()
                    except ValueError:
                        accepted = False
                if accepted != passing:
                    raise SystemExit("Register checker misclassified " + case["name"] + "\n" + output.getvalue())
                print(f"PASS {case['name']}: unguarded zero accepted, guarded={accepted}")


if __name__ == "__main__":
    main()
