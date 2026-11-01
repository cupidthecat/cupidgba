"""Require masked status at actual issue without accepting a zero-result shortcut."""

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
    with tempfile.TemporaryDirectory(prefix="cupid-status-observer-") as folder:
        rom = Path(folder) / "owned.gba"

        def image(flags):
            data = bytearray(256)
            # Set a known system-mode status and flags, then visit the watched
            # NOP and a zero-result checkpoint/terminal without failure writes.
            words = [
                0xE3A0C000,
                0xE321F01F,
                0xE59F0008,
                0xE128F000,
                0xE1A01001,
                0xEA000000,
                flags << 28,
                0xE1A0C00C,
                0xEAFFFFFE,
            ]
            struct.pack_into("<9I", data, 0, *words)
            rom.write_bytes(data)

        def run(options):
            return subprocess.run(
                [
                    str(runner),
                    str(rom),
                    "--frames",
                    "1",
                    "--steps",
                    "10000",
                    "--expect-loop-register",
                    "12",
                    "--checkpoint-address",
                    "0x0800001c",
                    "--terminal-address",
                    "0x08000020",
                    *options,
                ],
                capture_output=True,
                text=True,
                timeout=3,
            )

        for flags in range(16):
            image(flags)
            status = flags << 28 | 0x1F
            for mask in [0x80000000, 0xF0000000, 0xFF, 0xFFFFFFFF]:
                expected = status & mask
                options = ["--expect-status-at", "0x08000010", hex(mask), hex(expected)]
                result = run(options)
                record = (
                    f"observation=0 pc=08000010 mode=arm opcode=e1a01001 "
                    f"masked-cpsr={expected:08x} mask={mask:08x} expected={expected:08x} result=PASS"
                )
                assert result.returncode == 0 and record in result.stdout.splitlines(), (
                    result.stdout + result.stderr
                )
                assert "observations=1/1 result=PASS" in result.stdout
                wrong = expected ^ (mask & -mask)
                failed = run(options[:-1] + [hex(wrong)])
                assert failed.returncode == 1 and "result=FAIL" in failed.stdout, failed.stdout
                assert "checkpoint=yes r12=0 result=PASS" in failed.stdout
                assert "terminal=yes r12=0 result=PASS" in failed.stdout

        image(8)
        status = ["--expect-status-at", "0x08000010", "0x80000000", "0x80000000"]
        register = ["--expect-register-at", "0x0800001c", "12", "0"]
        mixed = run(status + register)
        assert mixed.returncode == 0 and "observations=2/2 result=PASS" in mixed.stdout
        reordered = run(register + status)
        assert reordered.returncode == 1 and "observation-order=FAIL" in reordered.stdout
        missing = run(status + ["--expect-status-at", "0x08000040", "1", "1"])
        assert missing.returncode == 1 and "observations=1/2 result=MISSING" in missing.stdout
        for fields in [
            [],
            ["0"],
            ["0", "1"],
            ["-1", "1", "0"],
            ["0x100000000", "1", "0"],
            ["0", "0", "0"],
            ["0", "-1", "0"],
            ["0", "0x100000000", "0"],
            ["0", "1", "2"],
            ["0", "1", "-1"],
            ["0", "0xffffffff", "0x100000000"],
        ]:
            assert run(["--expect-status-at", *fields]).returncode == 2, fields
        overflow = run(status * 129)
        assert overflow.returncode == 2
    print(
        "PASS masked status: all NZCV combinations, control bits, zero-result negatives, ordering and bounds"
    )


if __name__ == "__main__":
    main()
