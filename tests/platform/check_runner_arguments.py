#!/usr/bin/env python3
"""Reject signed, whitespace-prefixed and overflowing execution inputs before I/O."""

# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    runner = parser.parse_args().runner.resolve()
    count = 0
    with tempfile.TemporaryDirectory(prefix="cupidgba-runner-arguments-") as directory:
        root = Path(directory)
        rom = root / "input.gba"
        image = bytearray(512)
        struct.pack_into("<3I", image, 0, 0xE3A0C000, 0xE1A00000, 0xEAFFFFFE)
        rom.write_bytes(image)
        outputs = []
        flags = []
        for option in (
            "screenshot",
            "dump-vram",
            "dump-io",
            "dump-ewram",
            "dump-save",
            "dump-audio",
            "dump-rumble",
        ):
            path = root / option
            path.write_bytes(b"unchanged output")
            outputs.append(path)
            flags += ["--" + option, str(path)]
        contracts = [
            ("--steps", ["10"]),
            ("--frames", ["1"]),
            ("--start-address", ["0x08000000"]),
            ("--terminal-address", ["0x08000008"]),
            ("--checkpoint-address", ["0x08000004"]),
            ("--expect-loop-register", ["12"]),
            ("--key-event", ["0", "1"]),
            ("--expect-register-at", ["0x08000004", "12", "0"]),
            ("--expect-register-pair-at", ["0x08000004", "12", "0", "0", "0"]),
            ("--expect-status-at", ["0x08000004", "0xff", "0x1f"]),
        ]
        for option, arguments in contracts:
            for index in range(len(arguments)):
                for malformed in (
                    " -1",
                    "+" + arguments[index],
                    " " + arguments[index],
                    "18446744073709551616",
                ):
                    bad = arguments.copy()
                    bad[index] = malformed
                    result = subprocess.run(
                        [str(runner), str(rom), *flags, option, *bad],
                        capture_output=True,
                        text=True,
                        timeout=3,
                    )
                    count += 1
                    assert result.returncode == 2, (option, bad, result.stdout, result.stderr)
                    assert "Loaded ROM" not in result.stdout and "frames=" not in result.stdout
                    assert rom.read_bytes() == image
                    assert all(path.read_bytes() == b"unchanged output" for path in outputs)
        for budget in ("20", "024", "0x14", "18446744073709551615"):
            result = subprocess.run(
                [
                    str(runner),
                    str(rom),
                    "--steps",
                    budget,
                    "--frames",
                    "1",
                    "--start-address",
                    "0x08000000",
                    "--expect-loop-register",
                    "12",
                    "--checkpoint-address",
                    "0x08000004",
                    "--terminal-address",
                    "0x08000008",
                ],
                capture_output=True,
                text=True,
                timeout=3,
            )
            count += 1
            assert result.returncode == 0, result.stdout + result.stderr
            assert "checkpoint=yes r12=0 result=PASS" in result.stdout
            assert "terminal=yes r12=0 result=PASS" in result.stdout
    print(
        f"PASS {count} numeric argument runs: complete rejection before I/O and valid radix controls"
    )


if __name__ == "__main__":
    main()
