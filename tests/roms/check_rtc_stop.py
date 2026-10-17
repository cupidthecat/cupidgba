#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run a generated CupidGBA STOP/RTC integration probe with the replacement BIOS."""
import argparse
from pathlib import Path
import re
import struct
import subprocess
import tempfile


def cartridge():
    code, literals = [], []

    def emit(word):
        code.append(word)

    def load(register, value):
        literals.append((len(code), register, value))
        emit(0)

    def pins(value, offset=0):
        emit(0xE3A01000 | value)  # MOV r1,#value
        emit(0xE1C010B0 | offset)  # STRH r1,[r0,#offset]

    def byte(value):
        for bit in range(8):
            value_at_pin = 4 | ((value >> bit) & 1) << 1
            pins(value_at_pin)
            pins(value_at_pin | 1)

    load(0, 0x04000200)
    load(1, 0x2000)  # IE: Game Pak, IME remains clear.
    emit(0xE1C010B0)
    load(0, 0x080000C4)
    pins(7, 2)
    pins(1)
    pins(5)
    byte(0x16)
    byte(0)
    byte(0x80)  # Frequency parameters: 1 Hz divider.
    pins(1)
    pins(5)
    byte(0x46)
    byte(0x42)  # 24-hour, frequency output enabled.
    pins(1)
    load(0, 0x04000100)
    pins(0)
    pins(0x80, 2)
    emit(0xE1D050B0)  # LDRH r5,[r0], timer before STOP.
    emit(0xEF030000)  # SWI Stop.
    load(0, 0x04000100)
    emit(0xE1D060B0)
    emit(0xE0566005)  # SUBS r6,r6,r5
    emit(0xE1A06806)  # Retain the 16-bit timer difference.
    emit(0xE1A06826)
    load(7, 1024)
    emit(0xE1560007)
    emit(0xE3A0C001)
    emit(0x33A0C000)  # Passing timer difference is below 1024 running clocks.
    terminal = 0x08000100 + len(code) * 4
    emit(0xEAFFFFFE)
    for index, register, value in literals:
        displacement = (len(code) - index) * 4 - 8
        assert 0 <= displacement <= 4095
        code[index] = 0xE59F0000 | register << 12 | displacement
        emit(value)
    image = bytearray(4096)
    struct.pack_into("<I", image, 0, 0xEA00003E)
    struct.pack_into("<" + "I" * len(code), image, 0x100, *code)
    assert 0x100 + len(code) * 4 < 0xF00
    image[0xF00:0xF0B] = b"SIIRTC_V001"
    return image, terminal


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cupid-rtc-stop-") as directory:
        path = Path(directory) / "rtc-stop.gba"
        image, terminal = cartridge()
        path.write_bytes(image)
        result = subprocess.run(
            [args.runner, str(path), "--frames", "60", "--steps", "1000000",
             "--expect-loop-register", "12", "--terminal-address", hex(terminal)],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
        print(result.stdout, end="")
        elapsed = re.search(r"^stopped_cycles=(\d+)$", result.stdout, re.MULTILINE)
        if result.returncode or "terminal=yes r12=0 result=PASS" not in result.stdout:
            raise SystemExit("RTC STOP probe did not reach its passing terminal")
        if not elapsed or not 0 < int(elapsed[1]) <= 8388608:
            raise SystemExit("RTC STOP probe did not consume the expected battery-clock interval")


if __name__ == "__main__":
    main()
