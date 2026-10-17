#!/usr/bin/env python3
"""Exercise explicit Flash selection with an owned cartridge driver."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def cartridge(wrong_id=False):
    words, loads, branches, labels = [], [], [], {}

    def emit(word):
        words.append(word)

    def load(register, value):
        loads.append((len(words), register, value))
        emit(0)

    def label(name):
        labels[name] = len(words)

    def branch(name, condition=14):
        branches.append((len(words), name, condition))
        emit(0)

    def command(value):
        for register, byte in ((1, 0xAA), (2, 0x55), (1, value)):
            emit(0xE3A00000 | byte)              # mov r0, #byte
            emit(0xE5C00000 | register << 16)   # strb r0, [register]

    for register, value in ((1, 0x0E005555), (2, 0x0E002AAA), (3, 0x0E002345), (4, 0x0E000000)):
        load(register, value)
    command(0x90)
    emit(0xE3A0505A)  # default device writes a distinct failure image
    emit(0xE3A0C001)
    emit(0xE5D40000)
    emit(0xE3500000 | (0xBE if wrong_id else 0xBF))
    branch('exit-id', 1)
    emit(0xE5D40001)
    emit(0xE35000D4)
    branch('exit-id', 1)
    emit(0xE3A050A5)
    emit(0xE3A0C000)
    label('exit-id')
    command(0xF0)
    command(0xA0)
    emit(0xE5C35000)
    load(6, 4096)
    label('poll')
    emit(0xE5D30000)
    emit(0xE1500005)
    branch('done', 0)
    emit(0xE2566001)
    branch('poll', 1)
    emit(0xE3A0C002)
    label('done')
    emit(0xEAFFFFFE)
    for index, name, condition in branches:
        relative = labels[name] - index - 2
        words[index] = condition << 28 | 0x0A000000 | (relative & 0xFFFFFF)
    for index, register, value in loads:
        displacement = (len(words) - index - 2) * 4
        assert 0 <= displacement <= 4095
        words[index] = 0xE59F0000 | register << 12 | displacement
        words.append(value)
    image = bytearray(1024)
    struct.pack_into('<I', image, 0, 0xEA00002E)
    struct.pack_into('<' + 'I' * len(words), image, 0xC0, *words)
    assert 0xC0 + len(words) * 4 < 0x300
    image[0x300:0x30A] = b'FLASH512_V'
    return image, 0x080000C0 + labels['done'] * 4


def headless(runner, bios):
    with tempfile.TemporaryDirectory(prefix='cupidgba-flash-driver-') as directory:
        rom = Path(directory) / 'driver.gba'
        for wrong_id, explicit, expected in ((False, True, 0), (False, False, 1), (True, True, 1)):
            image, terminal = cartridge(wrong_id)
            rom.write_bytes(image)
            command = [str(runner), str(rom), '--frames', '2', '--steps', '100000',
                       '--expect-loop-register', '12', '--terminal-address', hex(terminal)]
            if explicit:
                command += ['--flash-profile', 'sst39vf512']
            if bios:
                # This driver qualifies the mapped firmware path, not boot.
                command += ['--bios', str(bios), '--start-address', '0x080000c0']
            result = subprocess.run(command, capture_output=True, text=True, timeout=3)
            required = f'terminal=yes r12={expected} result={"PASS" if not expected else "FAIL"}'
            if result.returncode != expected or required not in result.stdout:
                raise SystemExit(f'Flash driver result differs: {result.stdout}\n{result.stderr}')
        result = subprocess.run([str(runner), str(rom), '--flash-profile', 'invalid'],
                                capture_output=True, text=True, timeout=3)
        if result.returncode != 2:
            raise SystemExit('Invalid Flash profile was accepted')
    print('PASS Flash guest: explicit identity, byte command/poll loop, default and wrong-ID controls')


def frontend(program):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software')
    with tempfile.TemporaryDirectory(prefix='cupidgba-flash-frontend-') as directory:
        root = Path(directory)
        rom = root / 'driver.gba'
        rom.write_bytes(cartridge()[0])
        expected = bytearray(b'\xFF' * 65536)
        expected[0x2345] = 0xA5
        command = [str(program), str(rom), '-S', 'sst39vf512', '-s', '1']
        # Local sessions have an existing bounded frame interface. Keep the
        # single-console battery as a complete isolation sentinel.
        rom.with_suffix('.sav').write_bytes(b'\x69' * 65536)
        original = rom.with_suffix('.sav').read_bytes()
        result = subprocess.run(command + ['--link', str(rom), '--frames', '1'], env=env,
                                capture_output=True, text=True, timeout=10)
        if result.returncode or 'Local session finished: 1 frames' not in result.stdout:
            raise SystemExit(f'Local Flash profile failed: {result.stdout}\n{result.stderr}')
        for player in (1, 2):
            if rom.with_suffix(f'.p{player}.sav').read_bytes() != expected:
                raise SystemExit('A local console did not persist the complete selected Flash image')
        if rom.with_suffix('.sav').read_bytes() != original:
            raise SystemExit('Local Flash session changed the single-console battery')
        # Reject an incompatible existing image before either frontend runs.
        for local in (False, True):
            save = rom.with_suffix('.p2.sav' if local else '.sav')
            oversized = b'\x3C' * 131072
            save.write_bytes(oversized)
            before = {p: p.read_bytes() for p in root.glob('*.sav')}
            flags = ['--link', str(rom), '--frames', '1'] if local else []
            result = subprocess.run(command + flags, env=env, capture_output=True, text=True, timeout=3)
            if result.returncode != 1 or 'Cannot load save' not in result.stderr or 'CupidGBA started' in result.stdout:
                raise SystemExit('Incompatible Flash image did not stop startup')
            if any(p.read_bytes() != data for p, data in before.items()):
                raise SystemExit('Failed Flash startup changed a battery')
    print('PASS local Flash selection, two complete save images, battery isolation and single/local oversized startup rejection')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument('--runner', type=Path)
    target.add_argument('--frontend', type=Path)
    parser.add_argument('--bios', type=Path)
    args = parser.parse_args()
    if args.runner:
        headless(args.runner.resolve(), args.bios.resolve() if args.bios else None)
    else:
        if args.bios:
            parser.error('--bios requires --runner')
        frontend(args.frontend.resolve())


if __name__ == '__main__':
    main()
