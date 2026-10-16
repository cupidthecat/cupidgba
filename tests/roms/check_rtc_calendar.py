#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Require complete guest date/time GPIO transfers and independent calendar records."""
import argparse
import calendar
from pathlib import Path
import re
import struct
import subprocess
import tempfile

from check_rtc_parameters import Driver


class ContinuedThumbDriver(Driver):
    def load(self, register, value):
        # Extra frames exceed Thumb's literal-pool reach. Build constants in
        # registers only for these extended drivers, preserving every original
        # cartridge byte. No caller carries condition flags across a load.
        self.emit(0x2000 | register << 8 | (value >> 24 & 255))
        for shift in (16, 8, 0):
            self.emit(0x0200 | register << 3 | register)
            byte = value >> shift & 255
            if byte:
                self.emit(0x3000 | register << 8 | byte)


def bcd(value):
    return value // 10 * 16 + value % 10


def cases(rollover=False):
    # Expected bytes use the documented register format and Python's calendar,
    # independently of the emulated device's calendar and conversion helpers.
    dates = []
    for year in range(100):
        dates.append((year, 2, calendar.monthrange(2000 + year, 2)[1], year % 7,
                      year % 24, year % 60, (year * 7) % 60, bool(year & 1)))
    for mode in (False, True):
        for month in range(1, 13):
            for day in (1, calendar.monthrange(2024, month)[1]):
                dates.append((24, month, day, month % 7, month, day, 59, mode))
        for hour in range(24):
            dates.append((99, 12, 31, hour % 7, hour, 59, 0, mode))
    for index in range(60):
        dates.append((0, 1, 1, index % 7, index % 24, index, 59 - index, bool(index & 1)))
    if rollover:
        dates = [(99, 12, 31, 6, 10, 11, 12, True)]
    assert len(dates) == (1 if rollover else 256)
    table, expected = bytearray(), bytearray()
    for year, month, day, weekday, hour, minute, second, mode24 in dates:
        control = 0x40 if mode24 else 0

        def clock(h, m, s):
            return bytes((bcd(h if mode24 else h % 12) | (0x80 if h >= 12 else 0), bcd(m), bcd(s)))

        date = bytes((bcd(year), bcd(month), bcd(day), weekday)) + clock(hour, minute, second)
        # In 24-hour mode, deliberately supply the opposite AM/PM write bit.
        # The hour controls readback; in 12-hour mode that bit remains an input.
        written_date = bytearray(date)
        if mode24:
            written_date[4] ^= 0x80
        time = clock(23, 59, 59) if rollover else clock((hour + 13) % 24, (minute + 17) % 60, (second + 29) % 60)
        written_time = bytearray(time)
        if mode24:
            written_time[0] ^= 0x80
        final = date[:4] + time
        table += bytes((control,)) + written_date + date + written_time + time + final + bytes((control,))
        expected += date + time + final + bytes((control,))
    assert len(table) == len(dates) * 29 and len(expected) == len(dates) * 18
    if rollover:
        carried = bytes((0, 1, 1, 0, 0, 0, 0))
        table += carried
        expected += carried
    return table, expected


def cartridge(thumb, width, bank, present=True, wrong_expected=False, rollover=False, continued=False):
    code = ContinuedThumbDriver(thumb) if continued and thumb else Driver(thumb)
    table, expected = cases(rollover)
    rows = 1 if rollover else 256
    if wrong_expected:
        # Change only the first expected read byte in every row. The guest
        # still completes all transfers and must record exactly 256 failures.
        for row in range(rows):
            table[row * 29 + 8] ^= 1
    gpio = 0x080000C4 + bank * 0x02000000
    code.load(0, gpio)
    code.load(4, 0x08001000)
    code.load(5, 0x02000020)
    code.load(6, rows)
    code.emit(0x2700 if thumb else 0xE3A07000)
    serial = 0

    def immediate(register, value):
        if value < 256:
            code.emit((0x2000 | register << 8 | value) if thumb else (0xE3A00000 | register << 12 | value))
        else:
            code.load(register, value)

    def store_half(value, offset):
        immediate(1, value)
        code.emit((0x8001 | offset // 2 << 6) if thumb else (0xE1C010B0 | offset))

    def direction(mask):
        store_half(mask, 2)
        if width == 4:
            code.load(3, mask << 16)
            code.emit(0x4698 if thumb else 0xE1A08003)

    def pins(value):
        immediate(1, value)
        if width == 4:
            code.emit(0x4441 if thumb else 0xE1811008)  # Add the retained direction halfword.
        code.emit((0x6001 if thumb else 0xE5801000) if width == 4 else (0x8001 if thumb else 0xE1C010B0))

    def next_byte(register):
        if thumb:
            code.emit(0x7820 | register)
            code.emit(0x3401)
        else:
            code.emit(0xE4D40001 | register << 12)

    def send(value=None):
        if value is None:
            next_byte(1)
        else:
            immediate(1, value)
        immediate(2, 8)
        code.call('send')

    def command(value):
        direction(7)
        pins(1)
        pins(5)
        send(value)

    def read_bytes(count):
        nonlocal serial
        direction(5)
        for _ in range(count):
            code.call('read')
            if thumb:
                code.emit(0x7029)
                code.emit(0x3501)
            else:
                code.emit(0xE4C51001)
            next_byte(3)
            code.emit(0x4299 if thumb else 0xE1510003)
            name = f'match-{serial}'
            serial += 1
            code.branch(name, 0)
            code.emit(0x3701 if thumb else 0xE2877001)
            code.label(name)

    store_half(1, 4)
    code.label('row')
    command(0x46)
    send()  # Control: 12/24-hour mode, with interrupt outputs disabled.
    command(0x26)
    if continued:
        # A first complete payload must not terminate the selected stream.
        # Keep the original table, expected records and following payload intact.
        for value in (0, 1, 1, 0, 0, 0, 0):
            send(value)
    for _ in range(7):
        send()
    command(0xA6)
    read_bytes(7)
    command(0x66)
    if continued:
        for value in (0, 0, 0):
            send(value)
    for _ in range(3):
        send()
    command(0xE6)
    read_bytes(3)
    command(0xA6)
    read_bytes(7)
    command(0xC6)
    read_bytes(1)
    code.emit(0x3E01 if thumb else 0xE2566001)
    code.branch('done', 0)
    code.call('row')  # The main loop does not return; BL supplies a long Thumb branch.
    code.label('done')
    if rollover:
        code.label('poll-second')
        command(0xE6)
        direction(5)
        for _ in range(3):
            code.call('read')
        code.emit(0x2900 if thumb else 0xE3510000)
        code.branch('poll-second', 1)
        command(0xA6)
        read_bytes(7)
    code.load(0, 0x02000000)
    code.load(1, 0x43544331)
    code.emit(0x6001 if thumb else 0xE5801000)
    immediate(1, rows)
    code.emit(0x6041 if thumb else 0xE5801004)
    code.emit(0x6087 if thumb else 0xE5807008)
    code.emit(0x60C5 if thumb else 0xE580500C)
    code.emit(0x6104 if thumb else 0xE5804010)
    immediate(1, (bank << 16) | (width << 8) | thumb)
    code.emit(0x6141 if thumb else 0xE5801014)
    code.emit(0x46BC if thumb else 0xE1A0C007)
    code.load(0, 0x08000110)
    code.emit(0x4700 if thumb else 0xE12FFF10)
    code.label('send')
    code.label('send-bit')
    for op in ((0x07CB, 0x0F9B, 0x3304) if thumb else (0xE2013001, 0xE1A03083, 0xE3833004)):
        code.emit(op)
    if width == 4:
        code.emit(0x4443 if thumb else 0xE1833008)
    code.emit((0x6003 if thumb else 0xE5803000) if width == 4 else (0x8003 if thumb else 0xE1C030B0))
    code.emit(0x3301 if thumb else 0xE2833001)
    code.emit((0x6003 if thumb else 0xE5803000) if width == 4 else (0x8003 if thumb else 0xE1C030B0))
    code.emit(0x0849 if thumb else 0xE1A010A1)
    code.emit(0x3A01 if thumb else 0xE2522001)
    code.branch('send-bit', 1)
    code.emit(0x4770 if thumb else 0xE12FFF1E)
    code.label('read')
    immediate(1, 0)
    immediate(2, 8)
    code.label('read-bit')
    immediate(3, 4)
    if width == 4:
        code.emit(0x4443 if thumb else 0xE1833008)
    code.emit((0x6003 if thumb else 0xE5803000) if width == 4 else (0x8003 if thumb else 0xE1C030B0))
    code.emit((0x6803 if thumb else 0xE5903000) if width == 4 else (0x8803 if thumb else 0xE1D030B0))
    if thumb:
        for op in (0x079B, 0x0FDB, 0x01DB, 0x0849, 0x4319):
            code.emit(op)
    else:
        for op in (0xE2033002, 0xE1A03303, 0xE1A010A1, 0xE1811003):
            code.emit(op)
    immediate(3, 5)
    if width == 4:
        code.emit(0x4443 if thumb else 0xE1833008)
    code.emit((0x6003 if thumb else 0xE5803000) if width == 4 else (0x8003 if thumb else 0xE1C030B0))
    code.emit(0x3A01 if thumb else 0xE2522001)
    code.branch('read-bit', 1)
    code.emit(0x4770 if thumb else 0xE12FFF1E)
    image = bytearray(16384)
    struct.pack_into('<I', image, 0, 0xEA00003E)
    struct.pack_into('<III', image, 0x100, 0xE59F0000, 0xE12FFF10, 0x08000200 | thumb)
    struct.pack_into('<I', image, 0x110, 0xEAFFFFFE)
    image[0xAC:0xB0] = b'XXXX'
    if present:
        image[0x180:0x18B] = b'SIIRTC_V001'
    data = code.finish()
    assert 0x200 + len(data) <= 0x1000 and 0x1000 + len(table) <= len(image)
    image[0x200:0x200 + len(data)] = data
    image[0x1000:0x1000 + len(table)] = table
    return image, expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', required=True)
    parser.add_argument('--bios')
    parser.add_argument('--mode', choices=('arm', 'thumb'), required=True)
    parser.add_argument('--width', type=int, choices=(2, 4), required=True)
    parser.add_argument('--continued', action='store_true', help='Write two complete payloads without releasing chip select')
    args = parser.parse_args()
    thumb = args.mode == 'thumb'
    with tempfile.TemporaryDirectory(prefix='cupidgba-rtc-calendar-') as directory:
        root = Path(directory)
        configurations = [(b, True, False, False) for b in range(3)]
        configurations += [(0, False, False, False), (0, True, True, False)]
        configurations += [(b, True, False, True) for b in range(3)]
        for bank, present, wrong, rollover in configurations:
            image, expected = cartridge(thumb, args.width, bank, present, wrong, rollover, args.continued)
            rows = 1 if rollover else 256
            rom, ram = root / 'owned.gba', root / 'record.bin'
            rom.write_bytes(image)
            command = [args.runner, str(rom), '--frames', '120', '--steps', '5000000',
                       '--start-address', '0x08000000', '--terminal-address', '0x08000110',
                       '--expect-loop-register', '12', '--dump-ewram', str(ram)]
            if args.bios:
                command += ['--bios', args.bios]
            result = subprocess.run(command, capture_output=True, text=True, timeout=20)
            failures = 256 if wrong else sum(value != 0 for value in expected) if not present else 0
            if result.returncode != bool(failures) or f'terminal=yes r12={failures} result={"FAIL" if failures else "PASS"}' not in result.stdout:
                raise SystemExit('Calendar driver failed its complete terminal: ' + result.stdout + result.stderr)
            record = ram.read_bytes()
            header = (0x43544331, rows, failures, 0x02000020 + len(expected),
                      0x08001000 + rows * 29 + (7 if rollover else 0), (bank << 16) | (args.width << 8) | thumb)
            if len(record) != 0x40000 or struct.unpack_from('<6I', record) != header:
                raise SystemExit('Incomplete calendar driver counts or pointers')
            actual = record[0x20:0x20 + len(expected)]
            if actual != (expected if present else bytes(len(expected))):
                raise SystemExit('Calendar driver has incomplete or wrong complete serial bytes')
            if rollover:
                elapsed = re.search(r'^frames=\d+ cycles=(\d+) steps=\d+ pc=[0-9a-f]+$', result.stdout, re.M)
                if not elapsed or int(elapsed[1]) < 16777216:
                    raise SystemExit('Year rollover did not advance a complete RTC second')
            print(f'PASS {args.mode} width={args.width} bank={bank} present={present} wrong_expected={wrong}: '
                  f'{rows} complete date/time records, {len(expected)} serial bytes, {failures} expected failures'
                  + ('; complete one-second year rollover' if rollover else '')
                  + ('; continued calendar frames' if args.continued else ''))


if __name__ == '__main__':
    main()
