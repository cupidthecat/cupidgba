#!/usr/bin/env python3
"""Run owned Flash drivers through complete sector, bank and chip operations."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile


class Code:
    def __init__(self, thumb):
        self.thumb = thumb
        self.words, self.loads, self.branches, self.labels = [], [], [], {}
        self.counter = 0

    def emit(self, word):
        self.words.append(word)

    def label(self, name):
        self.labels[name] = len(self.words)

    def unique(self):
        self.counter += 1
        return str(self.counter)

    def load(self, register, value):
        self.loads.append((len(self.words), register, value))
        self.emit(0)

    def mov(self, register, value):
        assert 0 <= value <= 255
        self.emit((0x2000 | register << 8 | value) if self.thumb else (0xE3A00000 | register << 12 | value))

    def branch(self, label, condition):
        self.branches.append((len(self.words), label, condition))
        self.emit(0)

    def ldrb(self, dest, base):
        self.emit((0x7800 | base << 3 | dest) if self.thumb else (0xE5D00000 | base << 16 | dest << 12))

    def strb(self, source, base):
        self.emit((0x7000 | base << 3 | source) if self.thumb else (0xE5C00000 | base << 16 | source << 12))

    def compare(self, left, right):
        self.emit((0x4280 | right << 3 | left) if self.thumb else (0xE1500000 | left << 16 | right))

    def add_one(self, register):
        self.emit((0x3001 | register << 8) if self.thumb else (0xE2800001 | register << 16 | register << 12))

    def sub_one(self, register):
        self.emit((0x3801 | register << 8) if self.thumb else (0xE2500001 | register << 16 | register << 12))

    def stop(self, result):
        self.mov(0, result)
        self.load(1, 0x08000100)
        self.emit(0x4708 if self.thumb else 0xE12FFF11)

    def command(self, value):
        for register, byte in ((1, 0xAA), (2, 0x55), (1, value)):
            self.mov(0, byte)
            self.strb(0, register)

    def bank(self, value):
        self.command(0xB0)
        self.load(3, 0x0E000000)
        self.mov(0, value)
        self.strb(0, 3)

    def poll(self, failure):
        name = self.unique()
        self.load(7, 1000000)
        self.label('poll-' + name)
        self.ldrb(0, 3)
        self.compare(0, 6)
        self.branch('ready-' + name, 0)
        self.sub_one(7)
        self.branch('poll-' + name, 1)
        self.stop(failure)
        self.label('ready-' + name)

    def program(self, offset, source, length):
        name = self.unique()
        self.load(3, 0x0E000000 + offset)
        self.load(4, source)
        self.load(5, length)
        self.label('program-' + name)
        self.ldrb(6, 4)
        self.command(0xA0)
        self.strb(6, 3)
        self.poll(10)
        self.add_one(3)
        self.add_one(4)
        self.sub_one(5)
        self.branch('program-' + name, 1)

    def byte(self, offset, value):
        self.load(3, 0x0E000000 + offset)
        self.mov(6, value)
        self.command(0xA0)
        self.strb(6, 3)
        self.poll(11)

    def verify(self, offset, length, source=None, value=255, wrong=False, failure=2):
        name = self.unique()
        self.load(3, 0x0E000000 + offset)
        self.load(5, length)
        if source is not None:
            self.load(4, source)
        else:
            self.mov(6, value)
        self.label('verify-' + name)
        self.ldrb(0, 3)
        if source is not None:
            self.ldrb(6, 4)
        if wrong:
            if self.thumb:
                self.mov(7, 1)
                self.emit(0x407E)  # eor r6, r7
            else:
                self.emit(0xE2266001)
        self.compare(0, 6)
        self.branch('match-' + name, 0)
        self.stop(failure)
        self.label('match-' + name)
        self.add_one(3)
        if source is not None:
            self.add_one(4)
        self.sub_one(5)
        self.branch('verify-' + name, 1)

    def guard(self, address, value):
        name = self.unique()
        self.load(3, address)
        self.mov(5, 16)
        self.mov(6, value)
        self.label('guard-' + name)
        self.strb(6, 3)
        self.add_one(3)
        self.sub_one(5)
        self.branch('guard-' + name, 1)

    def copy_image(self, address):
        name = self.unique()
        self.load(3, 0x0E000000)
        self.load(4, address)
        self.load(5, 65536)
        self.label('copy-' + name)
        self.ldrb(0, 3)
        self.strb(0, 4)
        self.add_one(3)
        self.add_one(4)
        self.sub_one(5)
        self.branch('copy-' + name, 1)

    def finish(self):
        unit = 2 if self.thumb else 4
        for index, label, condition in self.branches:
            displacement = self.labels[label] - index - 2
            if self.thumb:
                assert -128 <= displacement <= 127
                self.words[index] = 0xD000 | condition << 8 | (displacement & 255)
            else:
                self.words[index] = condition << 28 | 0x0A000000 | (displacement & 0xFFFFFF)
        data = bytearray(struct.pack('<' + ('H' if self.thumb else 'I') * len(self.words), *self.words))
        while len(data) % 4:
            data.append(0)
        for index, register, value in self.loads:
            pc = (0x200 + index * unit + (4 if self.thumb else 8)) & ~3
            displacement = 0x200 + len(data) - pc
            if self.thumb:
                assert 0 <= displacement <= 1020 and not displacement % 4
                struct.pack_into('<H', data, index * 2, 0x4800 | register << 8 | displacement // 4)
            else:
                assert 0 <= displacement <= 4095
                struct.pack_into('<I', data, index * 4, 0xE59F0000 | register << 12 | displacement)
            data.extend(struct.pack('<I', value))
        return data


def pattern(seed):
    data = bytearray(((i * 37 + seed) ^ (i >> 4) ^ (i >> 8)) & 255 for i in range(4096))
    # A program payload at the unlock address must remain data. Every target
    # is erased before this stream; this byte does not reprogram old storage.
    data[0x555] = 0xAA
    return bytes(data)


def cartridge(thumb, large, wrong=False):
    code = Code(thumb)
    code.load(1, 0x0E005555)
    code.load(2, 0x0E002AAA)
    if large:
        code.bank(0)
    code.program(0x5000, 0x08002000, 4096)
    code.verify(0x5000, 4096, 0x08002000, wrong=wrong, failure=1)
    code.byte(0x4FFF, 0x69)
    code.byte(0x6000, 0x96)
    if large:
        code.bank(1)
        code.program(0x5000, 0x08003000, 4096)
        code.verify(0x5000, 4096, 0x08003000)
        code.byte(0x4FFF, 0x69)
        code.byte(0x6000, 0x96)
    code.command(0x80)
    for register, value in ((1, 0xAA), (2, 0x55)):
        code.mov(0, value)
        code.strb(0, register)
    code.load(3, 0x0E005000)
    code.mov(0, 0x30)
    code.strb(0, 3)
    code.mov(6, 0xFF)
    code.poll(12)
    code.verify(0x5000, 4096, failure=3)
    code.verify(0x4FFF, 1, value=0x69, failure=4)
    code.verify(0x6000, 1, value=0x96, failure=5)
    if large:
        code.bank(0)
        code.verify(0x5000, 4096, 0x08002000, failure=6)
        code.bank(1)
    code.command(0x80)
    code.command(0x10)
    code.load(3, 0x0E000000)
    code.mov(6, 0xFF)
    code.poll(13)
    if large:
        code.bank(0)
    code.verify(0, 65536, failure=7)
    if large:
        code.bank(1)
        code.verify(0, 65536, failure=8)
    code.program(0x5000, 0x08004000, 4096)
    code.verify(0x5000, 4096, 0x08004000, failure=9)
    size = 131072 if large else 65536
    code.guard(0x02000FF0, 0xA5)
    code.guard(0x02001000 + size, 0x5A)
    if large:
        code.bank(0)
    code.copy_image(0x02001000)
    if large:
        code.bank(1)
        code.copy_image(0x02011000)
    code.stop(0)
    body = code.finish()
    assert 0x200 + len(body) < 0x2000
    image = bytearray(0x5000)
    struct.pack_into('<I', image, 0, 0xEA00002E)
    struct.pack_into('<III', image, 0xC0, 0xE59F0000, 0xE12FFF10, 0x08000200 | int(thumb))
    struct.pack_into('<II', image, 0x100, 0xE1A0C000, 0xEAFFFFFE)
    marker = b'FLASH1M_V' if large else b'FLASH512_V'
    image[0x180:0x180 + len(marker)] = marker
    image[0x200:0x200 + len(body)] = body
    for offset, seed in ((0x2000, 0x31), (0x3000, 0x81), (0x4000, 0xC7)):
        image[offset:offset + 4096] = pattern(seed)
    expected = bytearray(b'\xFF' * size)
    offset = (65536 if large else 0) + 0x5000
    expected[offset:offset + 4096] = pattern(0xC7)
    return image, expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--profile', choices=('flash512', 'flash1m', 'sst39vf512'), required=True)
    parser.add_argument('--mode', choices=('arm', 'thumb'), required=True)
    parser.add_argument('--bios', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='cupidgba-flash-driver-') as directory:
        root = Path(directory)
        rom, dump = root / 'driver.gba', root / 'image.ram'
        for wrong in (False, True):
            image, expected = cartridge(args.mode == 'thumb', args.profile == 'flash1m', wrong)
            rom.write_bytes(image)
            command = [str(args.runner.resolve()), str(rom), '--frames', '120', '--steps', '50000000',
                       '--expect-loop-register', '12', '--terminal-address', '0x08000104', '--dump-ewram', str(dump)]
            if args.profile == 'sst39vf512':
                command += ['--flash-profile', 'sst39vf512']
            if args.bios:
                command += ['--bios', str(args.bios.resolve()), '--start-address', '0x080000c0']
            run = subprocess.run(command, capture_output=True, text=True, timeout=10)
            wanted = 'terminal=yes r12=1 result=FAIL' if wrong else 'terminal=yes r12=0 result=PASS'
            if run.returncode != int(wrong) or wanted not in run.stdout:
                raise SystemExit(f'Flash driver result differs: {run.stdout}\n{run.stderr}')
            if not wrong:
                data = dump.read_bytes()
                if len(data) != 262144 or data[0x1000:0x1000 + len(expected)] != expected:
                    raise SystemExit('Complete Flash image differs after sector/bank/chip transactions')
                if data[0xFF0:0x1000] != b'\xA5' * 16 or data[0x1000 + len(expected):0x1010 + len(expected)] != b'\x5A' * 16:
                    raise SystemExit('Flash image copy changed a guard')
    print(f'PASS {args.profile} {args.mode}: complete sector/bank/chip driver, full image, guards and wrong-comparison control')


if __name__ == '__main__':
    main()
