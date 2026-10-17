#!/usr/bin/env python3
"""Require independent local RTC batteries, options and startup isolation."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile

from check_local_startup import cartridge


def checksum(data, bits):
    value, prime = (2166136261, 16777619) if bits == 32 else (14695981039346656037, 1099511628211)
    for byte in data:
        value = ((value ^ byte) * prime) & ((1 << bits) - 1)
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frontend', type=Path, required=True)
    frontend = parser.parse_args().frontend.resolve()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software')
    with tempfile.TemporaryDirectory(prefix='cupidgba-local-rtc-') as directory:
        root = Path(directory)
        rom = root / 'clock.gba'
        image = cartridge(0x4A)
        rom.write_bytes(image)
        single = {rom.with_suffix('.sav'): b'single save', rom.with_suffix('.rtc'): b'single clock'}
        for path, data in single.items():
            path.write_bytes(data)
        paths = [rom.with_suffix(f'.p{i}.rtc') for i in range(1, 5)]
        command = [str(frontend), str(rom), *(['--link', str(rom)] * 3), '--frames', '1', '-s', '1']

        def run(*options, good=True):
            result = subprocess.run(command + list(options), env=env, capture_output=True, text=True, timeout=3)
            if good:
                if result.returncode or 'Local session finished: 1 frames' not in result.stdout:
                    raise SystemExit(f'Local RTC run failed: {result.stdout}\n{result.stderr}')
            elif result.returncode != 1 or 'Cannot load RTC state' not in result.stderr or 'CupidGBA started' in result.stdout:
                raise SystemExit('Invalid peer RTC state was accepted')

        run('--rtc', 'on', '--rtc-time', '2026-02-03T04:05:06')
        states = [path.read_bytes() for path in paths]
        for data in states:
            if (len(data) != 44 or data[:8] != b'CPDRTC01' or
                    int.from_bytes(data[8:16], 'little') != checksum(image, 64) or
                    data[16:24] != bytes((26, 2, 3, 2, 4, 5, 6, 0x40)) or
                    not 0 < int.from_bytes(data[24:28], 'little') < 16777216 or
                    data[28:40] != bytes((2, 0, 0, 0)) + bytes(8) or
                    int.from_bytes(data[40:], 'little') != checksum(data[:40], 32)):
                raise SystemExit('A local slot did not retain its complete seeded RTC image')
        if any(data != states[0] for data in states):
            raise SystemExit('Identical slots did not receive the same deterministic seed')
        changed = bytearray(states[1])
        changed[20] = 8
        struct.pack_into('<I', changed, 40, checksum(changed[:40], 32))
        paths[1].write_bytes(changed)
        run('--rtc', 'on')
        for i, path in enumerate(paths):
            if path.read_bytes()[16:23] != bytes((26, 2, 3, 2, 8 if i == 1 else 4, 5, 6)):
                raise SystemExit('A local RTC seed or battery leaked between slots')
        paths[3].write_bytes(b'invalid local clock')
        before = {path: path.read_bytes() for path in root.iterdir()}
        run('--rtc', 'on', good=False)
        if {path: path.read_bytes() for path in root.iterdir()} != before:
            raise SystemExit('Failed RTC startup changed a cartridge or battery')
        run('--rtc', 'off')
        if any(path.read_bytes() != before[path] for path in paths):
            raise SystemExit('Disabling RTC changed an existing battery')
        paths[3].write_bytes(states[3])
        run('--rtc', 'on', '--rtc-offline', 'wall', '--rtc-time', '2026-02-03T04:05:06')
        if any(not int.from_bytes(path.read_bytes()[32:40], 'little') for path in paths):
            raise SystemExit('Local wall-clock mode did not retain host anchors')
        if any(path.read_bytes() != data for path, data in single.items()):
            raise SystemExit('Local RTC play changed a single-console battery')
    print('PASS complete local RTC batteries, repeated cartridge, seed/options and failure isolation')


if __name__ == '__main__':
    main()
