"""Build an owned ARM/Thumb reader for an externally supplied RTC calendar."""

# SPDX-License-Identifier: GPL-3.0-or-later
import struct

from check_rtc_parameters import Driver


def cartridge(thumb, present=True):
    code = Driver(thumb)

    def immediate(register, value):
        code.emit(
            (0x2000 | register << 8 | value) if thumb else (0xE3A00000 | register << 12 | value)
        )

    def half(value, offset=0):
        immediate(1, value)
        code.emit((0x8001 | offset // 2 << 6) if thumb else (0xE1C010B0 | offset))

    code.load(0, 0x080000C4)
    code.load(5, 0x02000000)
    immediate(7, 0)
    half(1, 4)  # Enable GPIO reads.
    half(7, 2)  # Drive clock, data and chip select for the command.
    half(1)
    half(5)
    immediate(1, 0xA6)  # Read the seven-byte calendar without changing its mode or date.
    immediate(2, 8)
    code.call("send")
    half(5, 2)  # Release the data pin while keeping clock and chip select driven.
    for _ in range(7):
        code.call("read")
        if thumb:
            for word in (0x7029, 0x3501, 0x3701):
                code.emit(word)
        else:
            code.emit(0xE4C51001)
            code.emit(0xE2877001)
    half(1)
    code.load(0, 0x02000010)
    code.load(1, 0x52494331)
    for word in ((0x6001, 0x6047, 0x6085) if thumb else (0xE5801000, 0xE5807004, 0xE5805008)):
        code.emit(word)
    observation = 0x08000200 + len(code.words) * (2 if thumb else 4)
    immediate(1, 0)
    code.emit(0x468C if thumb else 0xE1A0C001)
    code.load(0, 0x08000110)
    code.emit(0x4700 if thumb else 0xE12FFF10)
    code.label("send")
    code.label("send-bit")
    for word in (
        (0x07CB, 0x0F9B, 0x3304, 0x8003, 0x3301, 0x8003, 0x0849, 0x3A01)
        if thumb
        else (
            0xE2013001,
            0xE1A03083,
            0xE3833004,
            0xE1C030B0,
            0xE2833001,
            0xE1C030B0,
            0xE1A010A1,
            0xE2522001,
        )
    ):
        code.emit(word)
    code.branch("send-bit", 1)
    code.emit(0x4770 if thumb else 0xE12FFF1E)
    code.label("read")
    immediate(1, 0)
    immediate(2, 8)
    code.label("read-bit")
    immediate(3, 4)
    code.emit(0x8003 if thumb else 0xE1C030B0)
    code.emit(0x8803 if thumb else 0xE1D030B0)
    for word in (
        (0x079B, 0x0FDB, 0x01DB, 0x0849, 0x4319)
        if thumb
        else (0xE2033002, 0xE1A03303, 0xE1A010A1, 0xE1811003)
    ):
        code.emit(word)
    immediate(3, 5)
    code.emit(0x8003 if thumb else 0xE1C030B0)
    code.emit(0x3A01 if thumb else 0xE2522001)
    code.branch("read-bit", 1)
    code.emit(0x4770 if thumb else 0xE12FFF1E)
    image = bytearray(8192)
    struct.pack_into("<I", image, 0, 0xEA00003E)
    struct.pack_into("<III", image, 0x100, 0xE59F0000, 0xE12FFF10, 0x08000200 | thumb)
    struct.pack_into("<I", image, 0x110, 0xEAFFFFFE)
    image[0xAC:0xB0] = b"XXXX"
    if present:
        image[0x180:0x18B] = b"SIIRTC_V001"
    body = code.finish()
    assert 0x200 + len(body) <= len(image)
    image[0x200 : 0x200 + len(body)] = body
    return image, observation


def stopped_cartridge(thumb):
    image = bytearray(1024)
    struct.pack_into("<I", image, 0, 0xEA00003E)
    # Supply an owned supervisor stack before entering either SWI caller mode.
    struct.pack_into(
        "<6I",
        image,
        0x100,
        0xE59FD008,
        0xE59F0008,
        0xE12FFF10,
        0xE1A00000,
        0x03007F00,
        0x08000120 | thumb,
    )
    if thumb:
        struct.pack_into("<2H", image, 0x120, 0xDF03, 0xE7FE)
    else:
        struct.pack_into("<2I", image, 0x120, 0xEF030000, 0xEAFFFFFE)
    image[0x180:0x18B] = b"SIIRTC_V001"
    return image
