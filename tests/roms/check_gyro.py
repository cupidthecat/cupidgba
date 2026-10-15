#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check complete generated ARM/Thumb gyroscope frames and zero overrun bits."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

from check_peripherals import Code


def cartridge(thumb, sample):
    code = Code(thumb)
    code.load(0, 0x080000C4)

    def pins(value, offset=0):
        code.emit((0x2100 if thumb else 0xE3A01000) | value)
        code.emit((0x8001 | offset // 2 << 6) if thumb else (0xE1C010B0 | offset))

    def delay():
        for _ in range(6 if thumb else 3):
            code.emit(0x46C0 if thumb else 0xE1A08008)

    pins(1, 4)
    pins(11, 2)
    code.load(6, sample << 16)
    code.emit(0x2200 if thumb else 0xE3A02000)
    code.emit(0x2700 if thumb else 0xE3A0C000)
    code.label("conversion")
    pins(3)
    delay()
    pins(2)
    delay()
    code.emit(0x2400 if thumb else 0xE3A04000)
    code.emit(0x2320 if thumb else 0xE3A03020)
    code.label("serial")
    code.emit(0x0064 if thumb else 0xE1A04084)  # Accumulate the whole 32-bit stream.
    code.emit(0x8805 if thumb else 0xE1D050B0)
    if thumb:
        for instruction in (0x08AD, 0x07ED, 0x0FED, 0x432C):
            code.emit(instruction)
    else:
        code.emit(0xE2055004)
        code.emit(0xE1844125)
    pins(0)
    delay()
    pins(2)
    delay()
    code.emit(0x3B01 if thumb else 0xE2533001)
    code.branch("serial", 1)
    code.emit(0x42B4 if thumb else 0xE1540006)
    code.branch("same", 0)
    code.emit(0x3701 if thumb else 0xE28CC001)
    code.label("same")
    code.emit(0x3201 if thumb else 0xE2822001)
    code.load(1, 256)
    code.emit(0x428A if thumb else 0xE1520001)
    code.branch("conversion", 1)
    code.load(0, 0x02000000)
    code.load(1, 0x47595231)
    for instruction in ((0x6001, 0x6042, 0x6087, 0x60C4, 0x46BC) if thumb else
                        (0xE5801000, 0xE5802004, 0xE580C008, 0xE580400C)):
        code.emit(instruction)
    code.load(0, 0x08000110)
    code.emit(0x4700 if thumb else 0xE12FFF10)
    image = bytearray(4096)
    struct.pack_into("<I", image, 0, 0xEA00003E)
    struct.pack_into("<III", image, 0x100, 0xE59F0000, 0xE12FFF10, 0x08000200 | thumb)
    struct.pack_into("<I", image, 0x110, 0xEAFFFFFE)
    image[0xAC:0xB0] = b"RZWE"
    data = code.finish()
    assert 0x200 + len(data) < len(image)
    image[0x200:0x200 + len(data)] = data
    return image


def run(runner, bios, rom, ram, sample, extra=()):
    command = [runner, str(rom), "--gyro-sample", str(sample), "--frames", "60",
               "--steps", "1000000", "--expect-loop-register", "12",
               "--start-address", "0x08000000", "--terminal-address", "0x08000110",
               "--dump-ewram", str(ram), *extra]
    if bios:
        command += ["--bios", bios]
    return subprocess.run(command, capture_output=True, text=True, timeout=20)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True)
    parser.add_argument("--bios")
    args = parser.parse_args()
    values = sorted({0, 4095, 0x6C0, 0x555, 0xAAA, *[1 << bit for bit in range(12)]})
    complete = 0
    with tempfile.TemporaryDirectory(prefix="cupid-gyro-") as directory:
        root = Path(directory)
        for thumb in (False, True):
            for sample in values:
                name = ("thumb" if thumb else "arm") + f"-{sample:03x}"
                rom, ram = root / (name + ".gba"), root / (name + ".bin")
                rom.write_bytes(cartridge(thumb, sample))
                result = run(args.runner, args.bios, rom, ram, sample)
                if result.returncode or "terminal=yes r12=0 result=PASS" not in result.stdout.splitlines():
                    raise SystemExit(name + " failed its original terminal: " + result.stdout + result.stderr)
                if ram.stat().st_size != 0x40000 or struct.unpack_from("<4I", ram.read_bytes()) != (
                        0x47595231, 256, 0, sample << 16):
                    raise SystemExit(name + " did not retain every complete frame")
                complete += 1
                print("PASS " + name + ": 256 complete 32-bit serial observations")
        rom, ram = root / "disabled.gba", root / "disabled.bin"
        rom.write_bytes(cartridge(False, 0x6C0))
        result = run(args.runner, args.bios, rom, ram, 0x6C0, ["--gyro", "off"])
        if result.returncode == 0 or "terminal=yes r12=256 result=FAIL" not in result.stdout.splitlines():
            raise SystemExit("Disabled gyroscope did not fail all original comparisons")
        for invalid in ("", "-1", "+1", "4096", "1.5", "0x6c0", " 1", "1 ", "99999999999999999999"):
            result = subprocess.run([args.runner, str(rom), "--gyro-sample", invalid],
                                    capture_output=True, timeout=20)
            if result.returncode != 2:
                raise SystemExit("Invalid gyroscope input was accepted")
    if complete != len(values) * 2:
        raise SystemExit("Incomplete gyroscope corpus")
    print(f"PASS {complete} driver cartridges and disabled/invalid-input controls")


if __name__ == "__main__":
    main()
