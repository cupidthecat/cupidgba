# @file test_dma_execution.py
# @brief Reject false-zero DMA results through the real checker and execution core.
"""Reject false-zero DMA results through the real checker and execution core."""

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

import check_dma_probe as probe
from protocols.comparisons import verify_input
from test_immediate_execution import cartridge


def register_cartridge(measured, expected, skip):
    data, case = cartridge(0x98, False, "arm", 12)
    words = [
        0xEA000003 if skip else 0xE59F0030,  # Load the measured value or skip the comparison.
        0xE59F2030,  # Load the unchanged expected control word.
        0xE1A00000 if expected else 0xE3A02000,  # Optional corruption of the live operand.
        0xE1A02822,  # LSR r2, r2, #16
        0xE1500002,  # CMP r0, r2
        0xE3A0C000,  # Deliberately hide any failed comparison in the result register.
        0xEA000008,  # Branch to the complete display/result entry.
    ]
    struct.pack_into("<7I", data, 0x100, *words)
    struct.pack_into("<2I", data, 0x138, measured, 0x84000001)
    case["observations"] = [
        dict(
            address="0x08000110",
            instruction="0xe1500002",
            mode="arm",
            register=0,
            second_register=2,
            value="0x8400",
            literal="0x0800013c",
            literal_shift=16,
        )
    ]
    case["sha256"] = hashlib.sha256(data).hexdigest()
    return data, case


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    args = parser.parse_args()
    scenarios = []
    for mode in ("arm", "thumb"):
        for name, value, skip, passed in (
            ("correct", 0x98, False, True),
            ("wrong-zero", 0, False, False),
            ("skipped", 0, True, False),
        ):
            scenarios.append((f"{mode}-{name}", *cartridge(value, skip, mode, 12), passed))
    for name, measured, expected, skip, passed in (
        ("correct", 0x8400, True, False, True),
        ("wrong-measurement", 0, True, False, False),
        ("wrong-expected", 0x8400, False, False, False),
        ("equal-but-wrong", 0, False, False, False),
        ("skipped", 0x8400, True, True, False),
    ):
        scenarios.append((f"pair-{name}", *register_cartridge(measured, expected, skip), passed))
    with tempfile.TemporaryDirectory(prefix="cupid-dma-observer-") as directory:
        for name, data, case, passed in scenarios:
            case.update(name=name, path=name + ".gba")
            verify_input(data, case)
            path = Path(directory) / case["path"]
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
            unguarded = subprocess.run(command, capture_output=True, text=True, timeout=5)
            if unguarded.returncode or "terminal=yes r12=0 result=PASS" not in unguarded.stdout:
                raise SystemExit(f"Owned zero-result control failed: {name}\n{unguarded.stdout}")
            output = io.StringIO()
            argv = [
                "check_dma_probe.py",
                "--directory",
                directory,
                "--runner",
                str(args.runner.resolve()),
                "--test",
                name,
            ]
            accepted = True
            with patch.object(sys, "argv", argv), patch.object(
                probe.json, "loads", return_value={"cases": [case]}
            ), contextlib.redirect_stdout(output):
                try:
                    probe.main()
                except SystemExit as error:
                    if not error.code:
                        raise
                    accepted = False
            if accepted != passed:
                raise SystemExit(f"DMA checker classification failed: {name}\n{output.getvalue()}")
            print(f"PASS {name}: unguarded zero accepted, complete checker accepted={accepted}")


if __name__ == "__main__":
    main()
