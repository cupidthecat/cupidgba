#!/usr/bin/env python3
"""Run local consoles with isolated batteries and reject unsafe startup inputs."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def cartridge(value):
    image = bytearray(512)
    struct.pack_into('<I', image, 0, 0xEA00002E)
    # One owned guest SRAM store followed by an infinite branch.
    for offset, word in enumerate((0xE59F0008, 0xE3A01000 | value, 0xE5C01000, 0xEAFFFFFE, 0x0E000000)):
        struct.pack_into('<I', image, 0xC0 + offset * 4, word)
    image[0x180:0x186] = b'SRAM_V'
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frontend', type=Path, required=True)
    frontend = parser.parse_args().frontend.resolve()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software')
    with tempfile.TemporaryDirectory(prefix='cupidgba-local-') as directory:
        root = Path(directory)
        roms = [root / f'console-{i}.gba' for i in range(4)]
        for i, rom in enumerate(roms):
            rom.write_bytes(cartridge(0x40 + i))
            rom.with_suffix('.sav').write_bytes(bytes([0xD0 + i]) * 32768)
        for count in range(2, 5):
            command = [str(frontend), str(roms[0])]
            for rom in roms[1:count]:
                command += ['--link', str(rom)]
            result = subprocess.run(command + ['--frames', '2', '-s', '1'], env=env,
                                    capture_output=True, text=True, timeout=15)
            if result.returncode or 'Local session finished: 2 frames' not in result.stdout:
                raise SystemExit(f'Local startup failed: {result.stdout}\n{result.stderr}')
            for i, rom in enumerate(roms[:count]):
                data = rom.with_suffix(f'.p{i + 1}.sav').read_bytes()
                if len(data) != 32768 or data[0] != 0x40 + i or data[1:] != bytes([0xFF]) * 32767:
                    raise SystemExit('A local console did not retain its complete SRAM image')
            for i, rom in enumerate(roms):
                if rom.with_suffix('.sav').read_bytes() != bytes([0xD0 + i]) * 32768:
                    raise SystemExit('Local play changed a single-console battery')
        # The same cartridge can occupy both slots without sharing a battery.
        result = subprocess.run([str(frontend), str(roms[0]), '--link', str(roms[0]), '--frames', '1', '-s', '1'],
                                env=env, capture_output=True, text=True, timeout=10)
        if result.returncode or not roms[0].with_suffix('.p2.sav').is_file():
            raise SystemExit('Repeated cartridge paths did not receive separate batteries')
        bad = roms[1].with_suffix('.p2.sav')
        bad.write_bytes(b'invalid save')
        before = {path: path.read_bytes() for path in root.glob('*.sav')}
        result = subprocess.run([str(frontend), str(roms[0]), '--link', str(roms[1]), '--frames', '1'],
                                env=env, capture_output=True, text=True, timeout=3)
        if result.returncode != 1 or 'Cannot load save' not in result.stderr or 'CupidGBA started' in result.stdout:
            raise SystemExit('Unsafe peer save was not rejected before starting all consoles')
        if any(path.read_bytes() != data for path, data in before.items()):
            raise SystemExit('Failed startup changed an existing battery')
    print('PASS local startup: two through four consoles, complete batteries, repeated path and failure isolation')


if __name__ == '__main__':
    main()
