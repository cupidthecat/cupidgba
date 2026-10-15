#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Observe live RTC parameter writes through complete guest GPIO/IRQ drivers."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

from check_peripherals import Code


class Driver(Code):
    def __init__(self, thumb):
        super().__init__(thumb)
        self.calls = []

    def call(self, label):
        self.calls.append((len(self.words), label))
        self.emit(0)
        if self.thumb:
            self.emit(0)

    def finish(self):
        for index, label in self.calls:
            if self.thumb:
                offset = (self.labels[label] - index) * 2 - 4
                assert -(1 << 22) <= offset < 1 << 22
                self.words[index] = 0xF000 | ((offset >> 12) & 0x7FF)
                self.words[index + 1] = 0xF800 | ((offset >> 1) & 0x7FF)
            else:
                offset = self.labels[label] - index - 2
                self.words[index] = 0xEB000000 | (offset & 0xFFFFFF)
        return super().finish()


def cartridge(thumb, mode, present=True):
    code = Driver(thumb)

    def immediate(register, value):
        if value < 256:
            code.emit((0x2000 | register << 8 | value) if thumb else
                      (0xE3A00000 | register << 12 | value))
        else:
            code.load(register, value)

    def half(value, offset=0):
        immediate(1, value)
        code.emit((0x8001 | offset // 2 << 6) if thumb else (0xE1C010B0 | offset))

    def send(value, count):
        immediate(1, value)
        immediate(2, count)
        code.call("send")

    def command(value):
        half(1)
        half(5)
        send(value, 8)

    def control(value):
        command(0x46)
        send(value, 8)

    code.load(0, 0x080000C4)
    code.load(4, 0x04000202)
    code.load(5, 0x2000)
    half(1, 4)
    immediate(6, 0)
    immediate(7, 0)
    code.label("observation")
    half(1)
    half(7, 2)
    control(0x40)
    command(0x16)
    send(0 if mode == "nominal" else 0x8000, 16)
    control(0x42)
    command(0x96 if mode == "nominal" else 0x16)
    if mode == "nominal":
        half(5, 2)
        immediate(2, 16)
        code.call("read")
    elif mode == "partial":
        send(0, 15)
    half(1)
    half(7, 2)
    code.emit(0x8025 if thumb else 0xE1C450B0)  # Acknowledge IF before the observation.
    if mode == "nominal":
        control(0x40)  # A release edge reveals that incoming ones changed the mask.
    else:
        immediate(2, 512)
        code.label("wait")
        code.emit(0x3A01 if thumb else 0xE2522001)
        code.branch("wait", 1)
    code.emit(0x8823 if thumb else 0xE1D430B0)
    code.emit(0x402B if thumb else 0xE0033005)
    code.emit((0x2B00 if mode == "unchanged" else 0x42AB) if thumb else
              (0xE3530000 if mode == "unchanged" else 0xE1530005))
    code.branch("matched", 0)
    code.emit(0x3701 if thumb else 0xE2877001)
    code.label("matched")
    code.emit(0x3601 if thumb else 0xE2866001)
    code.emit(0x2E20 if thumb else 0xE3560020)
    code.branch("observation", 1)
    code.load(0, 0x02000000)
    code.load(1, 0x52545031)
    for instruction in ((0x6001, 0x6046, 0x6087, 0x60C3, 0x46BC) if thumb else
                        (0xE5801000, 0xE5806004, 0xE5807008, 0xE580300C, 0xE1A0C007)):
        code.emit(instruction)
    code.load(0, 0x08000110)
    code.emit(0x4700 if thumb else 0xE12FFF10)
    code.label("send")
    code.label("send_bit")
    for instruction in ((0x07CB, 0x0F9B, 0x3304, 0x8003, 0x3301, 0x8003, 0x0849, 0x3A01) if thumb else
                        (0xE2013001, 0xE1A03083, 0xE3833004, 0xE1C030B0,
                         0xE3833001, 0xE1C030B0, 0xE1A010A1, 0xE2522001)):
        code.emit(instruction)
    code.branch("send_bit", 1)
    code.emit(0x4770 if thumb else 0xE12FFF1E)
    code.label("read")
    code.label("read_bit")
    for instruction in ((0x2104, 0x8001, 0x8803, 0x2102, 0x420B) if thumb else
                        (0xE3A01004, 0xE1C010B0, 0xE1D030B0, 0xE3130002)):
        code.emit(instruction)
    code.branch("read_high", 1)
    code.emit(0x3701 if thumb else 0xE2877001)
    code.label("read_high")
    for instruction in ((0x2105, 0x8001, 0x3A01) if thumb else
                        (0xE3A01005, 0xE1C010B0, 0xE2522001)):
        code.emit(instruction)
    code.branch("read_bit", 1)
    code.emit(0x4770 if thumb else 0xE12FFF1E)
    image = bytearray(8192)
    struct.pack_into("<I", image, 0, 0xEA00003E)
    struct.pack_into("<III", image, 0x100, 0xE59F0000, 0xE12FFF10, 0x08000200 | thumb)
    struct.pack_into("<I", image, 0x110, 0xEAFFFFFE)
    image[0xAC:0xB0] = b"XXXX"
    if present:
        image[0x180:0x18B] = b"SIIRTC_V001"
    data = code.finish()
    assert 0x200 + len(data) < len(image)
    image[0x200:0x200 + len(data)] = data
    return image


def run(args, rom, ram):
    command = [args.runner, str(rom), "--frames", "60", "--steps", "1000000",
               "--start-address", "0x08000000", "--terminal-address", "0x08000110",
               "--expect-loop-register", "12", "--dump-ewram", str(ram)]
    if args.bios:
        command += ["--bios", args.bios]
    return subprocess.run(command, capture_output=True, text=True, timeout=20)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True)
    parser.add_argument("--bios")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cupid-rtc-parameters-") as directory:
        root = Path(directory)
        for thumb in [False, True]:
            for mode in ["partial", "unchanged", "nominal"]:
                name = ("thumb" if thumb else "arm") + "-" + mode
                rom, ram = root / (name + ".gba"), root / (name + ".bin")
                rom.write_bytes(cartridge(thumb, mode))
                result = run(args, rom, ram)
                if result.returncode or "terminal=yes r12=0 result=PASS" not in result.stdout.splitlines():
                    raise SystemExit(name + " failed its terminal: " + result.stdout + result.stderr)
                expected = (0x52545031, 32, 0, 0 if mode == "unchanged" else 0x2000)
                if ram.stat().st_size != 0x40000 or struct.unpack_from("<4I", ram.read_bytes()) != expected:
                    raise SystemExit(name + " lacks its complete 32-observation record")
                print("PASS " + name + ": 32 complete IRQ observations" +
                      (" and 512 released serial-bit comparisons" if mode == "nominal" else ""))
        for mode, failures in [("partial", 32), ("nominal", 544)]:
            rom, ram = root / "absent.gba", root / "absent.bin"
            rom.write_bytes(cartridge(False, mode, present=False))
            result = run(args, rom, ram)
            if not result.returncode or f"terminal=yes r12={failures} result=FAIL" not in result.stdout.splitlines():
                raise SystemExit("Absent device did not fail every positive observation")
    print("PASS six complete drivers and absent-device controls")


if __name__ == "__main__":
    main()
