"""Owned ARM backup transactions for startup-selection and persistence checks."""

# SPDX-License-Identifier: GPL-3.0-or-later
import struct


class Driver:
    def __init__(self):
        self.words, self.loads, self.branches, self.labels = [], [], [], {}
        self.image = bytearray(8192)
        struct.pack_into("<I", self.image, 0, 0xEA00002E)

    def emit(self, word):
        self.words.append(word)

    def load(self, register, value):
        self.loads.append((len(self.words), register, value))
        self.emit(0)

    def label(self, name):
        self.labels[name] = len(self.words)

    def branch(self, name, condition=14):
        self.branches.append((len(self.words), name, condition))
        self.emit(0)

    def byte(self, address, value):
        self.load(0, address)
        self.load(1, value)
        self.emit(0xE5C01000)

    def command(self, value):
        for address, byte in ((0x0E005555, 0xAA), (0x0E002AAA, 0x55), (0x0E005555, value)):
            self.byte(address, byte)

    def transfer(self, offset, bits):
        struct.pack_into(f"<{len(bits)}H", self.image, offset, *bits)
        # EEPROM DMA reads an internal-RAM staging buffer. ROM-to-cartridge
        # transfers share the external burst counter and are not this protocol.
        self.load(0, 0x08000000 + offset)
        self.load(1, 0x03001000)
        self.load(4, len(bits))
        self.label(f"stage-{offset}")
        self.emit(0xE0D070B2)  # LDRH r7,[r0],#2.
        self.emit(0xE0C170B2)  # STRH r7,[r1],#2.
        self.emit(0xE2544001)
        self.branch(f"stage-{offset}", 1)
        self.load(0, 0x040000D4)
        self.load(1, 0x03001000)
        self.load(2, 0x0D000000)
        self.load(3, 0x80400000 | len(bits))
        self.emit(0xE880000E)  # STMIA r0,{r1-r3}: DMA3 source, destination, control.

    def finish(self):
        self.emit(0xE3A0C000)
        terminal = 0x080000C0 + len(self.words) * 4
        self.emit(0xEAFFFFFE)
        for index, name, condition in self.branches:
            self.words[index] = (
                condition << 28 | 0x0A000000 | ((self.labels[name] - index - 2) & 0xFFFFFF)
            )
        for index, register, value in self.loads:
            distance = (len(self.words) - index - 2) * 4
            assert 0 <= distance <= 4095
            self.words[index] = 0xE59F0000 | register << 12 | distance
            self.words.append(value)
        assert 0xC0 + len(self.words) * 4 < 0x1000
        struct.pack_into(f"<{len(self.words)}I", self.image, 0xC0, *self.words)
        return bytes(self.image), terminal


def cartridge(kind, value=0x5A, read_only=False):
    code = Driver()
    code.emit(0xE3A0C001)
    code.load(5, 0x02000000)
    if kind.startswith("eeprom"):
        width = 6 if kind == "eeprom512" else 14
        block = 63 if width == 6 else 1023
        address = [(block >> bit) & 1 for bit in reversed(range(width))]
        payload = bytes((value + index * 17) & 255 for index in range(8))
        if not read_only:
            bits = (
                [1, 0]
                + address
                + [(byte >> bit) & 1 for byte in payload for bit in reversed(range(8))]
                + [0]
            )
            code.transfer(0x1000, bits)
            code.label("busy")
            code.emit(0xE1D240B0)  # LDRH r4,[r2]: ready bit after programming.
            code.emit(0xE3540001)
            code.branch("busy", 1)
        code.transfer(0x1200, [1, 1] + address + [0])
        code.load(6, 68)
        code.label("read")
        code.emit(0xE1D240B0)
        code.emit(0xE0C540B2)  # STRH r4,[r5],#2.
        code.emit(0xE2566001)
        code.branch("read", 1)
        observed = struct.pack(
            "<68H",
            *([0] * 4 + [(byte >> bit) & 1 for byte in payload for bit in reversed(range(8))]),
        )
        offset = block * 8
    else:
        bank = kind == "flash1m"
        flash = kind.startswith("flash") or kind == "sst39vf512"
        offset = 0xF123 if kind == "sram512" else 0x123
        if flash:
            code.command(0x90)
            code.load(2, 0x0E000000)
            code.emit(0xE5D24000)
            code.emit(0xE5C54004)
            code.emit(0xE5D24001)
            code.emit(0xE5C54005)
            code.command(0xF0)
        if bank:
            code.command(0xB0)
            code.byte(0x0E000000, 1)
        if not read_only:
            if flash:
                code.command(0xA0)
            code.byte(0x0E000000 + offset, value)
        code.load(2, 0x0E000000 + offset)
        code.label("busy")
        code.emit(0xE5D24000)
        code.emit(0xE3540000 | value)
        if not read_only:
            code.branch("busy", 1)
        code.emit(0xE5C54000)
        payload = bytes([value])
        observed = payload
        if flash:
            identity = {"flash512": b"\x32\x1b", "flash1m": b"\x62\x13", "sst39vf512": b"\xbf\xd4"}[
                kind
            ]
            observed += bytes(3) + identity
        offset += 65536 if bank else 0
    image, terminal = code.finish()
    return image, terminal, offset, payload, observed
