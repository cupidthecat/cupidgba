#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# @file check_frontend_replay.py
# @brief Compare SDL2 and runner replays through complete guest-written SRAM images.
"""Compare SDL2 and runner replays through complete guest-written SRAM images."""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile

from check_runner_frames import cartridge as held_dma_cartridge


def dma_cartridge():
    image = bytearray(1024)
    base = held_dma_cartridge()
    image[:len(base)] = base
    # Continue the owned two-VBlank DMA fixture into an SRAM observation.
    struct.pack_into('<I', image, 0x1B8, 0xEA000010)
    words = [0xE59F002C, 0xE59F202C]
    for offset in range(4):
        words += [0xE5D01000 | offset, 0xE5C21000 | offset]
    words += [0xE3A0105A, 0xE5C21004, 0xEAFFFFFE, 0x02000000, 0x0E000000]
    struct.pack_into('<15I', image, 0x200, *words)
    image[0x280:0x286] = b'SRAM_V'
    return bytes(image)

def cartridge(thumb):
    image = bytearray(512)
    struct.pack_into('<I', image, 0, 0xEA00002E)
    position = 0xCC if thumb else 0xC0
    if thumb:
        struct.pack_into('<3I', image, 0xC0, 0xE59F0000, 0xE12FFF10, 0x080000CD)
    literals = []

    def emit(word):
        nonlocal position
        at = position
        struct.pack_into('<H' if thumb else '<I', image, position, word)
        position += 2 if thumb else 4
        return at

    def literal(register, value):
        literals.append((emit(0), register, value))

    def store(offset):
        emit(0x7011 | (offset << 6) if thumb else 0xE5C21000 | offset)

    def sample(offset, source=0):
        emit((0x8801 | (source << 3)) if thumb else (0xE1D010B0 | (source << 16)))
        store(offset)
        emit(0x0A09 if thumb else 0xE1A01421)
        store(offset + 1)

    literal(0, 0x04000130)
    literal(2, 0x0E000000)
    literal(3, 0x4001)
    emit(0x8043 if thumb else 0xE1C030B2)  # KEYCNT selects A, IRQ enabled.
    sample(0)
    for index, wanted in enumerate((0xEF, 0xFD)):
        loop = position
        emit(0x7801 if thumb else 0xE5D01000)
        emit((0x2900 if thumb else 0xE3510000) | wanted)
        at = position
        displacement = (loop - at - (4 if thumb else 8)) // (2 if thumb else 4)
        emit((0xD100 | (displacement & 255)) if thumb else
             (0x1A000000 | (displacement & 0xFFFFFF)))
        sample(2 + index * 2)
        if not index:
            literal(3, 0x04000202)
            sample(7, 3)
    emit(0x215A if thumb else 0xE3A0105A)
    store(6)
    emit(0xE7FE if thumb else 0xEAFFFFFE)
    position = (position + 3) & ~3
    for at, register, value in literals:
        struct.pack_into('<I', image, position, value)
        distance = position - (((at + 4) & ~3) if thumb else at + 8)
        instruction = (0x4800 | (register << 8) | distance // 4) if thumb else (
            0xE59F0000 | (register << 12) | distance)
        struct.pack_into('<H' if thumb else '<I', image, at, instruction)
        position += 4
    assert position <= 0x180
    image[0x180:0x186] = b'SRAM_V'
    return bytes(image)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frontend', type=Path, required=True)
    parser.add_argument('--runner', type=Path, required=True)
    args = parser.parse_args()
    frontend, runner = args.frontend.resolve(), args.runner.resolve()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               SDL_RENDER_DRIVER='software')
    script_bytes = b'0 4\n1 1\n1 16\n2 2\n'
    prefix = bytes((0xFB, 3, 0xEF, 3, 0xFD, 3, 0x5A, 0, 0x10))
    with tempfile.TemporaryDirectory(prefix='cupidgba-frontend-replay-') as folder:
        root = Path(folder)
        script = root / 'buttons.txt'
        script.write_bytes(script_bytes)

        def run(executable, rom, extra=(), status=0):
            result = subprocess.run([str(executable), str(rom), '--frames', '3', *extra],
                                    capture_output=True, text=True, timeout=10, env=env)
            assert result.returncode == status, result.stdout + result.stderr
            return result

        for thumb in (False, True):
            rom = root / ('thumb.gba' if thumb else 'arm.gba')
            original = cartridge(thumb)
            rom.write_bytes(original)
            save = rom.with_suffix('.sav')
            for seed in (bytes([255]) * 32768,
                         bytes((index * 13 + 0x37) & 255 for index in range(32768))):
                expected = prefix + seed[len(prefix):]
                save.write_bytes(seed)
                run(frontend, rom, ['--key-script', str(script)])
                assert save.read_bytes() == expected
                run(frontend, rom, ['--key-script', str(script)])
                assert save.read_bytes() == expected
                reference = root / 'runner.sav'
                battery_input = root / 'battery-input.sav'
                battery_input.write_bytes(seed)
                run(runner, rom, ['--key-script', str(script), '--load-save', str(battery_input),
                                  '--dump-save', str(reference)])
                assert reference.read_bytes() == expected
                assert battery_input.read_bytes() == seed
                assert rom.read_bytes() == original and script.read_bytes() == script_bytes

            # A missing intermediate A press must remove the IRQ observation.
            # The final held buttons alone cannot qualify file-order semantics.
            script.write_bytes(b'0 4\n1 16\n2 2\n')
            save.write_bytes(seed)
            run(frontend, rom, ['--key-script', str(script)])
            assert save.read_bytes() == prefix[:8] + b'\x00' + seed[9:]
            script.write_bytes(script_bytes)

            script.write_bytes(b'')
            save.write_bytes(seed)
            run(frontend, rom, ['--key-script', str(script)])
            assert save.read_bytes() == b'\xFF\x03' + seed[2:] and script.read_bytes() == b''
            script.write_bytes(script_bytes)
            for consoles in (2, 3, 4):
                primary_save = rom.with_suffix('.p1.sav')
                primary_save.write_bytes(seed)
                peers, peer_saves, flags = [], [], []
                for player in range(2, consoles + 1):
                    peer = root / f'peer{player}.gba'
                    peer.write_bytes(original)
                    peer_save = peer.with_suffix(f'.p{player}.sav')
                    peer_save.write_bytes(seed)
                    peers.append(peer)
                    peer_saves.append(peer_save)
                    flags += ['--link', str(peer)]
                run(frontend, rom, [*flags, '--key-script', str(script)])
                assert primary_save.read_bytes() == expected
                assert all(path.read_bytes() == b'\xFF\x03' + seed[2:] for path in peer_saves)
                assert all(path.read_bytes() == original for path in peers)
                assert rom.read_bytes() == original

        rom = root / 'held-dma.gba'
        original = dma_cartridge()
        rom.write_bytes(original)
        save = rom.with_suffix('.sav')
        save.write_bytes(seed)
        run(frontend, rom, ['--key-script', str(script)])
        expected = b'\xEF\x03\xFD\x03\x5A' + seed[5:]
        assert save.read_bytes() == expected
        battery_input.write_bytes(seed)
        run(runner, rom, ['--key-script', str(script), '--load-save', str(battery_input),
                          '--dump-save', str(reference)])
        assert reference.read_bytes() == expected and battery_input.read_bytes() == seed
        assert rom.read_bytes() == original

        guarded = [rom.with_suffix(suffix) for suffix in ('.sav', '.rtc', '.p1.sav')]
        for path in guarded:
            path.write_bytes(b'preserved output')
        bad = root / 'bad.txt'
        bad.write_bytes(b'0 0\x00ignored\n')
        oversized = root / 'oversized.txt'
        oversized.write_bytes(b'0 0\n' * 65537)
        missing = root / 'missing.txt'
        invalid = [["--key-script", str(path)] for path in (bad, oversized, missing, root)]
        invalid += [['--key-script'], ['--key-script', str(script), '--key-script', str(script)]]
        if hasattr(os, 'mkfifo'):
            fifo = root / 'pipe.txt'
            os.mkfifo(fifo)
            invalid.append(['--key-script', str(fifo)])
        for flags in invalid:
            result = run(frontend, rom, flags, status=1)
            assert 'Loaded ROM' not in result.stdout
            assert all(path.read_bytes() == b'preserved output' for path in guarded)
        assert script.read_bytes() == script_bytes
    print('PASS SDL2 ARM/Thumb replay, transient IRQs, DMA-held frame changes, complete SRAM, reboots, '
          'first-console ownership and invalid-input preservation')


if __name__ == '__main__':
    main()
