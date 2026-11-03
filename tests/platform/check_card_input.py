#!/usr/bin/env python3
# @file check_card_input.py
# @brief Execute owned card scanning guests and preserve inputs/outputs on rejected card files.
"""Execute owned card scanning guests and reject invalid card inputs before output creation."""

# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile


class Program:
    def __init__(self, thumb):
        self.thumb = thumb
        self.base = 0x080000D0 if thumb else 0x080000C0
        self.code = bytearray()
        self.pending, self.values, self.branches, self.labels = [], [], [], {}

    def emit(self, instruction):
        if self.pending and len(self.code) - self.pending[0][0] > 600:
            self.pool()
        self.code += struct.pack("<H" if self.thumb else "<I", instruction)

    def pool(self):
        if not self.pending:
            return
        branch = len(self.code)
        self.code += bytes(2 if self.thumb else 4)
        if len(self.code) % 4:
            self.code += struct.pack("<H", 0x46C0)
        for position, register, value in self.pending:
            literal = len(self.code)
            self.values.append((literal, value))
            self.code += bytes(4)
            pc = (self.base + position + 4) & ~3 if self.thumb else self.base + position + 8
            offset = self.base + literal - pc
            assert offset >= 0
            if self.thumb:
                assert offset % 4 == 0 and offset // 4 <= 255
                struct.pack_into("<H", self.code, position, 0x4800 | register << 8 | offset // 4)
            else:
                assert offset <= 4095
                struct.pack_into("<I", self.code, position, 0xE59F0000 | register << 12 | offset)
        self.pending.clear()
        self.patch_branch(branch, len(self.code), False)

    def patch_branch(self, position, destination, conditional):
        if self.thumb:
            displacement = (destination - position - 4) // 2
            assert (-128 <= displacement <= 127) if conditional else (-1024 <= displacement <= 1023)
            struct.pack_into("<H", self.code, position,
                             (0xD100 | displacement & 255) if conditional else (0xE000 | displacement & 2047))
        else:
            displacement = (destination - position - 8) // 4
            struct.pack_into("<I", self.code, position,
                             (0x1A000000 if conditional else 0xEA000000) | displacement & 0xFFFFFF)

    def ldr(self, register, value):
        self.emit(0x46C0 if self.thumb else 0xE1A00000)
        self.pending.append((len(self.code) - (2 if self.thumb else 4), register, value))

    def mov(self, register, value):
        self.emit((0x2000 | register << 8 | value) if self.thumb else (0xE3A00000 | register << 12 | value))

    def store_byte(self, offset=0, register=1):
        self.emit((0x7000 | offset << 6 | register) if self.thumb else (0xE5C00000 | register << 12 | offset))

    def pins(self, value):
        self.mov(1, value | 0x60)
        self.store_byte()

    def begin(self):
        for value in (7, 6, 4):
            self.pins(value)

    def send(self, value):
        for bit in range(7, -1, -1):
            data = 4 | (value >> bit & 1)
            for level in (data, data | 2, data):
                self.pins(level)
        for level in (0, 2, 0, 4):
            self.pins(level)

    def end(self):
        for value in (4, 6, 7):
            self.pins(value)

    def label(self, name):
        self.labels[name] = len(self.code)

    def branch(self, name, conditional=False):
        self.emit(0x46C0 if self.thumb else 0xE1A00000)
        self.branches.append((len(self.code) - (2 if self.thumb else 4), name, conditional))

    def record(self, address, offset, byte=False):
        self.ldr(0, address)
        self.emit((0x7801 if byte else 0x8801) if self.thumb else (0xE5D01000 if byte else 0xE1D010B0))
        self.emit((0x6021 | (offset // 4) << 6) if self.thumb else (0xE5841000 | offset))

    def image(self):
        self.ldr(0, 0x04000204)
        self.ldr(1, 0x5803)
        self.emit(0x8001 if self.thumb else 0xE1C010B0)
        self.ldr(0, 0x0E00FFB0)
        for index, value in ((0x14, 0), (0x15, 16)):
            self.begin()
            for item in (0x22, index, value):
                self.send(item)
            self.end()
        self.mov(1, 8)
        self.store_byte(2)
        self.mov(1, 0)
        self.store_byte(3)
        self.pins(0x78)
        self.mov(1, 0)
        for _ in range(42):
            self.store_byte(1)
        self.ldr(0, 0x040000D4)
        for offset, value in ((0, 0x0DFC0000), (4, 0x02000100), (8, 0x80000014)):
            self.ldr(1, value)
            self.emit((0x6001 | (offset // 4) << 6) if self.thumb else (0xE5801000 | offset))
        self.ldr(4, 0x02000000)
        self.ldr(0, 0x0DF80000)
        self.mov(1, 0xA5)
        self.emit(0x8001 if self.thumb else 0xE1C010B0)
        self.record(0x0DF80000, 0)
        self.record(0x0E00FFB1, 4, True)
        self.record(0x0DFA0000, 8)
        self.record(0x04000202, 12)
        self.record(0x0E00D000, 16, True)
        self.ldr(0, 0x02000100)
        self.ldr(3, "payload")
        self.mov(2, 20)
        self.label("compare")
        self.emit(0x8801 if self.thumb else 0xE1D010B0)
        self.emit(0x881D if self.thumb else 0xE1D350B0)
        self.emit(0x42A9 if self.thumb else 0xE1510005)
        self.branch("failed", True)
        self.emit(0x3002 if self.thumb else 0xE2800002)
        self.emit(0x3302 if self.thumb else 0xE2833002)
        self.emit(0x3A01 if self.thumb else 0xE2522001)
        self.branch("compare", True)
        self.mov(7, 0x5A)
        self.branch("publish")
        self.label("failed")
        self.mov(7, 0xE1)
        self.label("publish")
        self.emit((0x6027 | 5 << 6) if self.thumb else 0xE5847014)
        for address, value in ((0x0E005555, 0xAA), (0x0E002AAA, 0x55), (0x0E005555, 0xA0)):
            self.ldr(0, address)
            self.mov(1, value)
            self.store_byte()
        self.ldr(0, 0x0E000000)
        self.store_byte(register=7)
        self.label("idle")
        self.branch("idle")
        self.pool()
        if len(self.code) % 4:
            self.code += bytes(2)
        self.label("payload")
        # The scanline publishes white as one, with the first pixel in the last byte.
        self.code += struct.pack("<20H", *([0] * 10 + [0xFF0F] + [0xFFFF] * 9))
        for position, value in self.values:
            struct.pack_into("<I", self.code, position,
                             self.base + self.labels[value] if isinstance(value, str) else value)
        for position, name, conditional in self.branches:
            self.patch_branch(position, self.labels[name], conditional)
        data = bytearray(0xC0)
        struct.pack_into("<I", data, 0, 0xEA00002E)
        data[0xAC:0xB0] = b"PSAE"
        if self.thumb:
            data += struct.pack("<4I", 0xE3A0C000, 0xE59F7000, 0xE12FFF17, 0x080000D1)
        data += self.code
        return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--frontend", type=Path)
    args = parser.parse_args()
    runner = args.runner.resolve()
    environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software")
    with tempfile.TemporaryDirectory(prefix="cupidgba-card-input-") as temporary:
        folder = Path(temporary)
        card = folder / "owned card.bin"
        card.write_bytes(bytes([255]) * 3520)
        original = card.read_bytes()
        for thumb in (False, True):
            rom = folder / ("thumb.gba" if thumb else "arm.gba")
            rom.write_bytes(Program(thumb).image())
            original_rom = rom.read_bytes()
            ram, save = folder / "ram.bin", folder / "headless.sav"
            command = [str(runner), str(rom), "--frames", "2", "--steps", "100000",
                       "--start-address", "0x080000C0", "--card", str(card),
                       "--dump-ewram", str(ram), "--dump-save", str(save)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=10, env=environment)
            assert result.returncode == 0, result.stdout + result.stderr
            assert "frames=2 " in result.stdout
            expected_ram = bytearray(256 * 1024)
            struct.pack_into("<6I", expected_ram, 0, 5, 0x82, 4, 0x2000, ord("C"), 0x5A)
            struct.pack_into("<20H", expected_ram, 0x100, *([0] * 10 + [0xFF0F] + [0xFFFF] * 9))
            assert ram.read_bytes() == expected_ram, (thumb, ram.read_bytes()[:24].hex())
            battery = save.read_bytes()
            assert len(battery) == 128 * 1024 and battery[0] == 0x5A
            assert battery[0xD000:0xD012] == b"Card-E Reader 2001"
            assert battery[0xD000:0xE000] == battery[0xE000:0xF000]
            repeated = subprocess.run(command, capture_output=True, text=True, timeout=10, env=environment)
            assert repeated.returncode == 0, repeated.stdout + repeated.stderr
            assert repeated.stdout == result.stdout and ram.read_bytes() == expected_ram and save.read_bytes() == battery
            saved_input = folder / "preserved input.sav"
            initial = bytearray([255]) * (128 * 1024)
            initial[0xD001] = 0x5A
            saved_input.write_bytes(initial)
            loaded = subprocess.run(command + ["--load-save", str(saved_input)], capture_output=True,
                                    text=True, timeout=10, env=environment)
            assert loaded.returncode == 0, loaded.stdout + loaded.stderr
            expected_loaded_ram = bytearray(expected_ram)
            struct.pack_into("<I", expected_loaded_ram, 16, 255)
            expected_loaded_battery = bytearray(battery)
            expected_loaded_battery[0xD000:0xE000] = initial[0xD000:0xE000]
            assert ram.read_bytes() == expected_loaded_ram
            assert save.read_bytes() == expected_loaded_battery and saved_input.read_bytes() == initial
            blank_card = folder / "blank card.bin"
            blank_card.write_bytes(bytes(3520))
            wrong = [str(runner), str(rom), "--frames", "2", "--steps", "100000",
                     "--start-address", "0x080000C0", "--card", str(blank_card),
                     "--dump-ewram", str(ram), "--dump-save", str(save)]
            failed_guest = subprocess.run(wrong, capture_output=True, text=True, timeout=10, env=environment)
            assert failed_guest.returncode == 0, failed_guest.stdout + failed_guest.stderr
            wrong_ram = bytearray(expected_ram)
            struct.pack_into("<I", wrong_ram, 20, 0xE1)
            wrong_ram[0x100:0x128] = bytes([255]) * 40
            assert ram.read_bytes() == wrong_ram and save.read_bytes()[0] == 0xE1
            if args.frontend:
                frontend = subprocess.run([str(args.frontend.resolve()), str(rom), "--frames", "2",
                                           "--card", str(card)], capture_output=True, text=True,
                                          timeout=10, env=environment)
                assert frontend.returncode == 0, frontend.stdout + frontend.stderr
                assert rom.with_suffix(".sav").read_bytes() == battery
            assert card.read_bytes() == original and rom.read_bytes() == original_rom

        outputs = [("--screenshot", "picture.ppm"), ("--dump-ewram", "ewram.bin"),
                   ("--dump-vram", "vram.bin"), ("--dump-io", "io.bin"),
                   ("--dump-save", "backup.sav"), ("--dump-audio", "audio.pcm"),
                   ("--dump-rumble", "rumble.bin")]
        guarded = [str(runner), str(rom), "--frames", "1"]
        for option, name in outputs:
            path = folder / name
            path.write_bytes(b"preserved output")
            guarded += [option, str(path)]
        bad = folder / "bad.bin"
        bad.write_bytes(b"unsupported")
        oversized = folder / "oversized.bin"
        oversized.write_bytes(bytes(16 * 2912 + 1))
        cases = [["--card", str(path)] for path in (bad, oversized, folder, folder / "missing")]
        cases += [["--card", str(card), "--card", str(bad)],
                  ["--card", str(card), "--card-reader", "off"],
                  ["--card-reader", "ON"], ["--card"],
                  ["--card", str(card)] * 17]
        for arguments in cases:
            result = subprocess.run(guarded + arguments, capture_output=True, text=True, timeout=3, env=environment)
            assert result.returncode == 2 and "Loaded ROM" not in result.stdout, result.stdout + result.stderr
            assert all((folder / name).read_bytes() == b"preserved output" for _, name in outputs)
            assert card.read_bytes() == original
    frontend_scope = ", SDL2 persistence" if args.frontend else ""
    print("PASS ARM/Thumb card scan, complete DMA/RAM/backup, deterministic repeats"
          + frontend_scope + " and invalid-input preservation")


if __name__ == "__main__":
    main()
