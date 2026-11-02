#!/usr/bin/env python3
# @file check_runner_cycles.py
# @brief Require complete repeated guest observations and retain failures after the minimum.
"""Require complete repeated guest observations, including late failures."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile


def cartridge(late_failure=False, skip_second=False):
    # Four loops followed by a zero-result idle branch. A later failure must
    # remain visible even after the requested minimum cycle count has passed.
    words = [0xE3A02000, 0xE3A00011, 0xE3520002,
             0x03A00012 if late_failure else 0xE1A00000,
             0xE3A01022, 0xEA000000 if skip_second else 0xE1A00000,
             0xE1A00000, 0xE2822001, 0xE3520004, 0xBAFFFFF6,
             0xE3A0C000, 0xEAFFFFFE]
    return bytes(0xC0) + struct.pack("<12I", *words)


def thumb_cartridge():
    data = bytearray(0x104)
    struct.pack_into("<3I", data, 0xC0, 0xE3A0C000, 0xE59F3010, 0xE12FFF13)
    struct.pack_into("<I", data, 0xDC, 0x080000E1)
    # Four Thumb loops, then BX to an ARM zero-result terminal.
    struct.pack_into("<10H", data, 0xE0, 0x2200, 0x2011, 0x2122, 0x46C0, 0x46C0,
                     0x3201, 0x2A04, 0xD1F8, 0x4B01, 0x4718)
    struct.pack_into("<I", data, 0xF8, 0x08000100)
    struct.pack_into("<I", data, 0x100, 0xEAFFFFFE)
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    runner = parser.parse_args().runner.resolve()
    with tempfile.TemporaryDirectory(prefix="cupidgba-observation-cycles-") as folder:
        folder = Path(folder)
        rom, output = folder / "owned.gba", folder / "ram.bin"
        command = [str(runner), str(rom), "--frames", "1", "--steps", "1000",
                   "--start-address", "0x080000C0",
                   "--expect-loop-register", "12", "--dump-ewram", str(output)]
        first = ["--expect-register-at", "0x080000D4", "0", "0x11"]
        second = ["--expect-register-at", "0x080000D8", "1", "0x22"]

        def run(minimum=None, data=None, expectations=None, terminal="0x080000EC", extra=()):
            rom.write_bytes(data or cartridge())
            args = command + (first + second if expectations is None else expectations)
            if minimum is not None:
                args += ["--repeat-observations", str(minimum)]
            return subprocess.run(args + ["--terminal-address", terminal] + list(extra),
                                  capture_output=True, text=True, timeout=3)

        for minimum in (1, 4):
            result = run(minimum)
            assert result.returncode == 0, result.stdout + result.stderr
            assert f"observation-cycles=4 minimum={minimum} partial=0/2 result=PASS" in result.stdout
            assert "terminal=yes r12=0 result=PASS" in result.stdout
        result = run(5)
        assert result.returncode == 1 and "result=MISSING" in result.stdout
        assert "terminal=yes r12=0 result=PASS" in result.stdout
        # The ordinary protocol still rejects extra visits after the sequence.
        result = run()
        assert result.returncode == 1 and "observation-order=FAIL" in result.stdout
        for data, expectations in ((cartridge(late_failure=True), first + second),
                                   (cartridge(skip_second=True), first + second),
                                   (cartridge(), second + first)):
            result = run(1, data, expectations)
            assert result.returncode == 1, result.stdout + result.stderr
            assert "terminal=yes r12=0 result=PASS" in result.stdout
            assert "observation-cycles=" in result.stdout and "result=FAIL" in result.stdout
        result = run(4, expectations=first)
        assert result.returncode == 0 and "partial=0/1 result=PASS" in result.stdout
        result = run(18446744073709551615)
        assert result.returncode == 1 and "result=MISSING" in result.stdout

        # A valid partial prefix never counts as a complete cycle. Meeting the
        # observation minimum also cannot override an unfinished execution guard.
        result = run(1, extra=["--steps", "15"])
        assert result.returncode == 1, result.stdout + result.stderr
        assert "observation-cycles=1 minimum=1 partial=1/2 result=PASS" in result.stdout, result.stdout
        assert "terminal-issued=no completed=no" in result.stdout
        for expectations in (["--expect-register-at", "0x080000E6", "0", "0x11",
                              "--expect-register-at", "0x080000E8", "1", "0x22"],
                             ["--expect-register-pair-at", "0x080000E6", "0", "0x11", "1", "0x22",
                              "--expect-status-at", "0x080000E8", "0x20", "0x20"]):
            result = run(4, thumb_cartridge(), expectations, terminal="0x08000100")
            assert result.returncode == 0, result.stdout + result.stderr
            assert "observation-cycles=4 minimum=4 partial=0/2 result=PASS" in result.stdout
            assert "pc=080000e6 mode=thumb opcode=000046c0" in result.stdout

        for value in ("0", "-1", "+1", " 1", "18446744073709551616"):
            output.write_bytes(b"preserved")
            result = run(value)
            assert result.returncode == 2 and "Loaded ROM" not in result.stdout
            assert output.read_bytes() == b"preserved"
        output.write_bytes(b"preserved")
        result = run(1, expectations=[])
        assert result.returncode == 2 and "Loaded ROM" not in result.stdout
        assert output.read_bytes() == b"preserved"
        result = run(4, extra=["--repeat-observations", "1"])
        assert result.returncode == 2 and output.read_bytes() == b"preserved"
    print("PASS repeated sequences, zero-result false positives, late/missing/reordered observations and invalid arguments")


if __name__ == "__main__":
    main()
