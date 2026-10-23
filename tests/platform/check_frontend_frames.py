#!/usr/bin/env python3
"""Check bounded single-console runs through ordinary frontend battery paths."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import os
from pathlib import Path
import re
import stat
import struct
import subprocess
import tempfile

from check_local_rtc import checksum


def cartridge(stop=False):
    image = bytearray(512)
    struct.pack_into('<I', image, 0, 0xEA00002E)
    # Increment the existing first SRAM byte, copy it to byte 1, then loop or
    # enter STOP. Every other byte must survive loading and the final flush.
    words = (0xE59F0028, 0xE5D01000, 0xE2811001, 0xE5C01000, 0xE5C01001,
             0xEF030000 if stop else 0xEAFFFFFE, 0xEAFFFFFE)
    for index, word in enumerate(words):
        struct.pack_into('<I', image, 0xC0 + index * 4, word)
    struct.pack_into('<I', image, 0xF0, 0x0E000000)
    image[0x180:0x186] = b'SRAM_V'
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frontend', type=Path, required=True)
    parser.add_argument('--observer', type=Path, required=True)
    args = parser.parse_args()
    frontend, observer = args.frontend.resolve(), args.observer.resolve()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               SDL_RENDER_DRIVER='software')
    env.pop('CUPID_TEST_QUIT_AFTER', None)
    seed = bytes((index * 13 + 0x37) & 255 for index in range(32768))
    expected = bytes([seed[0] + 1]) * 2 + seed[2:]
    with tempfile.TemporaryDirectory(prefix='cupidgba-frontend-frames-') as directory:
        root = Path(directory)

        def run(exe, rom, flags=(), quit_after=None, status=0):
            run_env = dict(env)
            if quit_after is not None:
                run_env['CUPID_TEST_QUIT_AFTER'] = str(quit_after)
            result = subprocess.run([str(exe), str(rom), '-s', '1', *flags],
                                    env=run_env, capture_output=True, text=True, timeout=3)
            if result.returncode != status:
                raise SystemExit(f'Wrong frontend exit: {result.returncode}\n{result.stdout}\n{result.stderr}')
            return result

        for count in (1, 2, 299, 300, 301):
            rom = root / f'intervals-{count}.gba'
            rom.write_bytes(cartridge(stop=count > 2))
            save = rom.with_suffix('.sav')
            save.write_bytes(seed)
            result = run(observer, rom, ('--frames', str(count)))
            periodic = count // 300
            record = (f'OBS polls={count} intervals={count} presents={count} '
                      f'flushes={periodic + 1} dirty=1 stopped={int(count > 2)}')
            if record not in result.stdout or save.read_bytes() != expected:
                raise SystemExit(f'Incorrect interval count or complete battery: {record}\n{result.stdout}')
            flushes = [(int(frame), int(dirty)) for frame, dirty in
                       re.findall(r'FLUSH intervals=(\d+) dirty=(\d+)', result.stdout)]
            expected_flushes = ([(300, 1), (count, 0)] if periodic else [(count, 1)])
            if flushes != expected_flushes:
                raise SystemExit(f'Wrong periodic/final flush boundary: {flushes}')
            if 'CupidGBA exited.' not in result.stdout or 'Error:' in result.stderr:
                raise SystemExit('Bounded run bypassed ordinary successful cleanup')

        # Actual shipped executable: fresh battery, then two independent
        # process reboots. No test observer supplies or writes guest bytes.
        rom = root / 'fresh.gba'
        rom.write_bytes(cartridge())
        save = rom.with_suffix('.sav')
        for value in range(3):
            result = run(frontend, rom, ('--frames', '2'))
            if (save.read_bytes() != bytes([value]) * 2 + b'\xFF' * 32766 or
                    'CupidGBA exited.' not in result.stdout or 'Error:' in result.stderr):
                raise SystemExit('Normal frontend process did not reload and flush its complete battery')

        rom = root / 'stopped.gba'
        rom.write_bytes(cartridge(stop=True))
        result = run(observer, rom, ('--frames', '3', '--rtc', 'on',
                                    '--rtc-time', '2026-02-03T04:05:06'))
        if ('OBS polls=3 intervals=3 presents=3 flushes=1 dirty=1 stopped=1' not in result.stdout or
                rom.with_suffix('.sav').read_bytes() != bytes(2) + b'\xFF' * 32766 or
                not rom.with_suffix('.rtc').is_file()):
            raise SystemExit('STOP prevented the interval bound or final battery flush')
        rtc = rom.with_suffix('.rtc').read_bytes()
        if (len(rtc) != 44 or rtc[:8] != b'CPDRTC01' or
                int.from_bytes(rtc[8:16], 'little') != checksum(cartridge(stop=True), 64) or
                rtc[16:24] != bytes((26, 2, 3, 2, 4, 5, 6, 0x40)) or
                int.from_bytes(rtc[24:28], 'little') != 3 * 280896 or
                rtc[28:40] != bytes((2, 0, 0, 0)) + bytes(8) or
                int.from_bytes(rtc[40:], 'little') != checksum(rtc[:40], 32)):
            raise SystemExit(f'Incorrect complete STOP RTC battery: {rtc.hex()}')

        rom = root / 'early.gba'
        rom.write_bytes(cartridge())
        for flags in ((), ('--frames', '300'), ('--frames', '4294967295')):
            rom.with_suffix('.sav').write_bytes(seed)
            result = run(observer, rom, flags, quit_after=2)
            if ('OBS polls=3 intervals=2 presents=2 flushes=1 dirty=1 stopped=0' not in result.stdout or
                    rom.with_suffix('.sav').read_bytes() != expected):
                raise SystemExit('Ordinary input quit did not retain its earlier shutdown boundary')

        for text in ('', '0', '-1', ' -2', '+1', '1x', '4294967296'):
            before = rom.with_suffix('.sav').read_bytes()
            result = run(frontend, rom, ('--frames', text), status=1)
            if ('Invalid frame count' not in result.stderr or 'CupidGBA started' in result.stdout or
                    rom.with_suffix('.sav').read_bytes() != before):
                raise SystemExit(f'Invalid single-console interval bound was accepted: {text!r}')
        run(frontend, rom, ('--frames',), status=1)

        # Read-only input remains readable. Guest writes dirty the emulated
        # battery; both periodic and final replacement failures stay visible.
        for count in (1, 300):
            rom.write_bytes(cartridge(stop=count > 1))
            save = rom.with_suffix('.sav')
            save.write_bytes(seed)
            save.chmod(stat.S_IREAD)
            try:
                result = run(observer, rom, ('--frames', str(count)), status=1)
                flushes = 1 + count // 300
                if (save.read_bytes() != seed or result.stderr.count('Error: Cannot save ') != flushes or
                        f'flushes={flushes} dirty={flushes}' not in result.stdout):
                    raise SystemExit('Bounded shutdown concealed a failed battery replacement')
            finally:
                save.chmod(stat.S_IREAD | stat.S_IWRITE)
        if list(root.glob('*.tmp.*')) or list(root.glob('*.p[1-4].sav')):
            raise SystemExit('Single-console runs left a staging file or used local-session batteries')
    print('PASS bounded frontend: exact intervals, periodic/final flush, process reboots, STOP, quit and failures')


if __name__ == '__main__':
    main()
