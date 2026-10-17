#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Require complete ARM/Thumb X/Y observations at the documented frame cadence."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

from check_peripherals import Code


def packed(x, y):
    return (x & 255) | ((x >> 8) | 0x80) << 8 | (y & 255) << 16 | (y >> 8) << 24


def cartridge(thumb, x, y):
    code = Code(thumb)

    def byte_write(address, value):
        code.load(0, address)
        code.emit((0x2100 if thumb else 0xE3A01000) | value)
        code.emit(0x7001 if thumb else 0xE5C01000)

    def half_write(address, value):
        code.load(0, address)
        code.load(1, value)
        code.emit(0x8001 if thumb else 0xE1C010B0)

    half_write(0x04000204, 0x0803)
    half_write(0x04000208, 0)
    half_write(0x04000200, 1)
    half_write(0x04000004, 8)
    code.load(0, 0x02000000)
    code.emit(0x2100 if thumb else 0xE3A01000)
    code.emit(0x6041 if thumb else 0xE5801004)
    code.load(6, packed(x, y))
    code.emit(0x2700 if thumb else 0xE3A07000)
    code.label("conversion")
    byte_write(0x0E008000, 0x55)
    byte_write(0x0E008100, 0xAA)
    half_write(0x04000202, 1)
    code.emit(0xDF02 if thumb else 0xEF020000)  # HALT until next VBlank; IME remains off.
    code.load(0, 0x0E008300)
    code.load(3, 256)
    code.label("poll")
    code.emit(0x7805 if thumb else 0xE5D05000)
    if thumb:
        code.emit(0x2180)
        code.emit(0x420D)
    else:
        code.emit(0xE3150080)
    code.branch("ready", 1)
    code.emit(0x3B01 if thumb else 0xE2533001)
    code.branch("poll", 1)
    code.label("ready")
    code.load(0, 0x0E008200)
    code.emit(0x7804 if thumb else 0xE5D04000)
    for shift, address in [(8, 0x0E008300), (16, 0x0E008400), (24, 0x0E008500)]:
        code.load(0, address)
        code.emit(0x7805 if thumb else 0xE5D05000)
        if thumb:
            code.emit(shift << 6 | 5 << 3 | 5)
            code.emit(0x432C)
        else:
            code.emit(0xE1844005 | shift << 7)
    code.emit(0x42B4 if thumb else 0xE1540006)
    code.branch("same", 0)
    code.emit(0x3701 if thumb else 0xE2877001)
    code.label("same")
    code.load(0, 0x02000000)
    code.emit(0x6842 if thumb else 0xE5902004)
    code.emit(0x3201 if thumb else 0xE2822001)
    code.emit(0x6042 if thumb else 0xE5802004)
    code.emit(0x2A20 if thumb else 0xE3520020)
    code.branch("conversion", 1)
    code.load(1, 0x544C5431)
    for instruction in ((0x6001, 0x6087, 0x60C4, 0x46BC) if thumb else
                        (0xE5801000, 0xE5807008, 0xE580400C, 0xE1A0C007)):
        code.emit(instruction)
    code.load(0, 0x08000110)
    code.emit(0x4700 if thumb else 0xE12FFF10)
    image = bytearray(4096)
    struct.pack_into("<I", image, 0, 0xEA00003E)
    struct.pack_into("<I", image, 0x100, 0xEA000006)
    struct.pack_into("<I", image, 0x110, 0xEAFFFFFE)
    # Explicit entry skips BIOS boot. Initialize banked stacks and system mode
    # before SWI, so the mapped firmware can enter and leave supervisor mode.
    struct.pack_into("<12I", image, 0x120,
                     0xE321F0D3, 0xE59FD014, 0xE321F0D2, 0xE59FD010,
                     0xE321F0DF, 0xE59FD00C, 0xE59F000C, 0xE12FFF10,
                     0x03007FE0, 0x03007FA0, 0x03007F00, 0x08000200 | thumb)
    image[0xAC:0xB0] = b"KYGE"
    image[0x180:0x18B] = b"EEPROM_V124"
    data = code.finish()
    assert 0x200 + len(data) < len(image)
    image[0x200:0x200 + len(data)] = data
    return image


def run(args, rom, ram, x, y, extra=()):
    command = [args.runner, str(rom), "--tilt-x", str(x), "--tilt-y", str(y),
               "--frames", "60", "--steps", "1000000", "--expect-loop-register", "12",
               "--start-address", "0x08000000", "--terminal-address", "0x08000110",
               "--dump-ewram", str(ram), *extra]
    if args.bios:
        command += ["--bios", args.bios]
    return subprocess.run(command, capture_output=True, text=True, timeout=20)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True)
    parser.add_argument("--bios")
    args = parser.parse_args()
    values = [(0, 4095), (0x3A0, 0x3A0), (0x555, 0xAAA), (4095, 0)]
    complete = 0
    with tempfile.TemporaryDirectory(prefix="cupid-tilt-") as directory:
        root = Path(directory)
        for thumb in (False, True):
            for x, y in values:
                name = ("thumb" if thumb else "arm") + f"-{x:03x}-{y:03x}"
                rom, ram = root / (name + ".gba"), root / (name + ".bin")
                rom.write_bytes(cartridge(thumb, x, y))
                result = run(args, rom, ram, x, y)
                if result.returncode or "terminal=yes r12=0 result=PASS" not in result.stdout.splitlines():
                    raise SystemExit(name + " failed its terminal: " + result.stdout + result.stderr)
                if ram.stat().st_size != 0x40000 or struct.unpack_from("<4I", ram.read_bytes()) != (
                        0x544C5431, 32, 0, packed(x, y)):
                    raise SystemExit(name + " did not retain all 32 complete paired observations")
                complete += 1
                print("PASS " + name + ": 32 complete paired snapshots at VBlank cadence")
        rom, ram = root / "disabled.gba", root / "disabled.bin"
        rom.write_bytes(cartridge(False, 0x3A0, 0x3A0))
        result = run(args, rom, ram, 0x3A0, 0x3A0, ["--tilt", "off"])
        if result.returncode == 0 or "terminal=yes r12=32 result=FAIL" not in result.stdout.splitlines():
            raise SystemExit("Disabled tilt device did not fail every paired comparison")
        for option in ["--tilt-x", "--tilt-y"]:
            for invalid in ["", "-1", "+1", "4096", "1.5", "0x3a0", " 1", "1 ", "99999999999999999999"]:
                result = subprocess.run([args.runner, str(rom), option, invalid], capture_output=True, timeout=20)
                if result.returncode != 2:
                    raise SystemExit("Invalid tilt input was accepted")
    if complete != 8:
        raise SystemExit("Incomplete tilt driver corpus")
    print("PASS eight complete drivers and disabled/invalid-input controls")


if __name__ == "__main__":
    main()
