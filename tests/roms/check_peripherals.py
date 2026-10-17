#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run generated ARM/Thumb solar and motor driver cartridges."""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile


class Code:
    def __init__(self, thumb):
        self.thumb, self.words, self.loads, self.branches, self.labels = thumb, [], [], [], {}

    def emit(self, value):
        self.words.append(value)

    def label(self, name):
        self.labels[name] = len(self.words)

    def load(self, register, value):
        self.loads.append((len(self.words), register, value))
        self.emit(0)

    def branch(self, name, condition):
        self.branches.append((len(self.words), name, condition))
        self.emit(0)

    def finish(self):
        unit = 2 if self.thumb else 4
        for index, name, condition in self.branches:
            relative = self.labels[name] - index - 2
            if self.thumb:
                assert -128 <= relative <= 127
                self.words[index] = 0xD000 | condition << 8 | (relative & 255)
            else:
                self.words[index] = condition << 28 | 0x0A000000 | (relative & 0xFFFFFF)
        data = bytearray(struct.pack("<" + ("H" if self.thumb else "I") * len(self.words), *self.words))
        while len(data) % 4:
            data.extend(b"\x00")
        for index, register, value in self.loads:
            pc = (0x200 + index * unit + (4 if self.thumb else 8)) & ~3
            displacement = 0x200 + len(data) - pc
            if self.thumb:
                assert 0 <= displacement <= 1020 and not displacement % 4
                struct.pack_into("<H", data, index * 2, 0x4800 | register << 8 | displacement // 4)
            else:
                assert 0 <= displacement <= 4095
                struct.pack_into("<I", data, index * 4, 0xE59F0000 | register << 12 | displacement)
            data.extend(struct.pack("<I", value))
        return data


def cartridge(thumb, solar):
    code = Code(thumb)
    code.load(0, 0x080000C4)

    def pins(value, offset=0):
        code.emit((0x2100 if thumb else 0xE3A01000) | value)
        code.emit((0x8001 | offset // 2 << 6) if thumb else (0xE1C010B0 | offset))

    pins(1, 4)
    pins(3 if solar else 8, 2)
    pins(2 if solar else 0)
    pins(0)
    code.emit(0x2200 if thumb else 0xE3A02000)  # Pulse count.
    code.emit(0x2700 if thumb else 0xE3A0C000)  # Failure count.
    if thumb:
        code.load(3, 256)
    code.label("pulse")
    if solar:
        pins(1)
        pins(0)
        code.emit(0x3201 if thumb else 0xE2822001)
        code.emit(0x8804 if thumb else 0xE1D040B0)
        code.emit(0x2A80 if thumb else 0xE3520080)
        code.emit(0x2500 if thumb else 0xE3A05000)
        if thumb:
            code.branch("below", 3)
            code.emit(0x2508)
            code.label("below")
            code.emit(0x429A)
            code.branch("wrapped", 3)
            code.emit(0x2500)
            code.label("wrapped")
        else:
            code.emit(0x23A05008)
            code.emit(0xE3520C01)
            code.emit(0x23A05000)
    else:
        code.emit(0x3201 if thumb else 0xE2822001)
        if thumb:
            for value in (0x4615, 0x2401, 0x4025, 0x00ED, 0x4629, 0x8001, 0x8804):
                code.emit(value)
        else:
            for value in (0xE2025001, 0xE1A05185, 0xE1A01005, 0xE1C010B0, 0xE1D040B0):
                code.emit(value)
    code.emit(0x42AC if thumb else 0xE1540005)
    if thumb:
        code.branch("match", 0)
        code.emit(0x3701)
        code.label("match")
        code.emit(0x429A)
    else:
        code.emit(0x128CC001)
        code.emit(0xE3520C01)
    code.branch("pulse", 3)
    code.load(6, 0x02000000)
    code.load(4, 0x534F4C31 if solar else 0x524D4231)
    if thumb:
        for value in (0x6034, 0x6072, 0x60B7, 0x46BC):
            code.emit(value)
        code.load(0, 0x08000110)
        code.emit(0x4700)
    else:
        for value in (0xE5864000, 0xE5862004, 0xE586C008):
            code.emit(value)
        code.load(0, 0x08000110)
        code.emit(0xE12FFF10)
    image = bytearray(4096)
    struct.pack_into("<I", image, 0, 0xEA00003E)
    struct.pack_into("<III", image, 0x100, 0xE59F0000, 0xE12FFF10, 0x08000200 | thumb)
    struct.pack_into("<I", image, 0x110, 0xEAFFFFFE)
    data = code.finish()
    assert 0x200 + len(data) < len(image)
    image[0x200:0x200 + len(data)] = data
    image[0xAC:0xB0] = b"U3IJ" if solar else b"V49E"
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True)
    parser.add_argument("--bios")
    args = parser.parse_args()
    complete = 0
    with tempfile.TemporaryDirectory(prefix="cupid-peripherals-") as directory:
        path = Path(directory)
        for thumb in (False, True):
            for solar in (False, True):
                name = ("thumb" if thumb else "arm") + ("-solar" if solar else "-rumble")
                rom, ram, motor = (path / (name + extension) for extension in (".gba", ".bin", ".rmb"))
                rom.write_bytes(cartridge(thumb, solar))
                command = [args.runner, str(rom), "--sunlight", "128", "--frames", "60",
                           "--steps", "1000000", "--expect-loop-register", "12",
                           "--start-address", "0x08000000",
                           "--terminal-address", "0x08000110", "--dump-ewram", str(ram)]
                if args.bios:
                    command.extend(["--bios", args.bios])
                if not solar:
                    command.extend(["--dump-rumble", str(motor)])
                result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        text=True, timeout=20)
                print(result.stdout, end="")
                if result.returncode or "terminal=yes r12=0 result=PASS" not in result.stdout:
                    raise SystemExit(name + " failed to reach its passing terminal")
                expected = (0x534F4C31 if solar else 0x524D4231, 256, 0)
                if struct.unpack_from("<III", ram.read_bytes()) != expected:
                    raise SystemExit(name + " did not record all 256 driver comparisons")
                if not solar:
                    data = motor.read_bytes()
                    if data[:8] != b"CPDRMB01" or len(data) != 8 + 258 * 12:
                        raise SystemExit(name + " has an incomplete motor trace")
                    records = list(struct.iter_unpack("<QB3x", data[8:]))
                    if [active for _, active in records] != [0] + [i % 2 for i in range(1, 257)] + [0]:
                        raise SystemExit(name + " has incorrect motor transitions")
                    if any(a[0] >= b[0] for a, b in zip(records[1:-1], records[2:-1])):
                        raise SystemExit(name + " has unordered motor strobe clocks")
                    print(name + " trace_sha256=" + hashlib.sha256(data).hexdigest())
                complete += 1
                print("PASS " + name + ": 256 complete driver comparisons")
    if complete != 4:
        raise SystemExit("Incomplete cartridge corpus")


if __name__ == "__main__":
    main()
