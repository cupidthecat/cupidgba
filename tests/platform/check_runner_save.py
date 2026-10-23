#!/usr/bin/env python3
"""Check explicit backup input through guest reads and independent processes."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def cartridge(path, marker, first=0x4C, second=0x6A, write=0x5A,
              base=0x0E000000, identity=False, transport=False):
    words, literals, failures, observations = [], [], [], []

    def emit(word):
        words.append(word)

    def literal(register, value):
        literals.append((len(words), register, value))
        emit(0)

    def compare(register, value):
        observations.append((0x08000000 + len(words) * 4, register, value))
        emit(0xE3500000 | register << 16 | value)
        failures.append(len(words))
        emit(0)

    emit(0xE3A0C001)  # A missing comparison must not produce a zero result.
    if not transport:
        literal(1, base)
        emit(0xE5D10000)  # LDRB r0,[r1]
        compare(0, first)
        emit(0xE5D12123)  # LDRB r2,[r1,123h]
        compare(2, second)
        if identity:
            literal(5, 0x0E005555)
            literal(6, 0x0E002AAA)
            for register, value in ((5, 0xAA), (6, 0x55), (5, 0x90)):
                emit(0xE3A03000 | value)
                emit(0xE5C03000 | register << 16)
            emit(0xE5D14000)  # Manufacturer and device remain the selected profile.
            compare(4, 0xBF)
            emit(0xE5D17001)
            compare(7, 0xD4)
            emit(0xE3A030F0)
            emit(0xE5C13000)
        else:
            if write is not None:
                emit(0xE3A03000 | write)
                emit(0xE5C13123)  # STRB r3,[r1,123h]
            emit(0xE5D14123)
            compare(4, second if write is None else write)
    emit(0xE3A0C000)
    checkpoint = 0x08000000 + len(words) * 4
    emit(0xE1A0C00C)
    terminal = 0x08000000 + len(words) * 4
    emit(0xEAFFFFFE)
    failure = len(words)
    emit(0xE3A0C001)
    index = len(words)
    emit(0xEA000000 | (((terminal - 0x08000000) // 4 - index - 2) & 0xFFFFFF))
    for index in failures:
        words[index] = 0x1A000000 | ((failure - index - 2) & 0xFFFFFF)
    for index, register, value in literals:
        offset = len(words) * 4 - index * 4 - 8
        words[index] = 0xE59F0000 | register << 12 | offset
        emit(value)
    assert len(words) * 4 < 0xA0
    image = bytearray(512)
    struct.pack_into(f'<{len(words)}I', image, 0, *words)
    image[0xA0:0xAC] = b'CPDSAVEINPUT'
    image[0x180:0x180 + len(marker)] = marker
    path.write_bytes(image)
    return checkpoint, terminal, observations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    runner = parser.parse_args().runner.resolve()
    count = 0
    with tempfile.TemporaryDirectory(prefix='cupidgba-runner-save-') as directory:
        root = Path(directory)
        rom, seed = root / 'owned.gba', root / 'input.sav'

        def run(protocol, source=None, extra=()):
            nonlocal count
            count += 1
            checkpoint, terminal, observations = protocol
            output = root / f'output-{count}.sav'
            command = [str(runner), str(rom), '--start-address', '0x08000000',
                       '--frames', '1', '--steps', '10000', '--expect-loop-register', '12',
                       '--checkpoint-address', hex(checkpoint), '--terminal-address', hex(terminal),
                       '--dump-save', str(output)]
            for address, register, value in observations:
                command += ['--expect-register-at', hex(address), str(register), hex(value)]
            if source is not None:
                command += ['--load-save', str(source)]
            result = subprocess.run(command + list(extra), capture_output=True, text=True, timeout=3)
            return result, output

        def passed(result):
            assert result.returncode == 0, result.stdout + result.stderr
            assert 'checkpoint=yes r12=0 result=PASS' in result.stdout.splitlines()
            assert 'terminal=yes r12=0 result=PASS' in result.stdout.splitlines()

        def pattern(size, base=0):
            data = bytearray((index * 13 + (index >> 8) * 7 + 29) & 255 for index in range(size))
            data[base] = 0x4C
            data[base + 0x123] = 0x6A
            return data

        # Guest instructions consume the loaded bytes and change one SRAM byte.
        # A second process loads that exact output and performs another write.
        for size, offset in ((32768, 0), (65536, 0xF000)):
            payload = pattern(size, offset)
            seed.write_bytes(payload)
            protocol = cartridge(rom, b'SRAM_V', base=0x0E000000 + offset)
            cold, cold_output = run(protocol)
            assert cold.returncode == 1 and 'r0=000000ff expected=0000004c result=FAIL' in cold.stdout
            assert cold_output.read_bytes() == bytes([255]) * 32768
            first, first_output = run(protocol, seed)
            passed(first)
            expected = payload.copy()
            expected[offset + 0x123] = 0x5A
            assert first_output.read_bytes() == expected and seed.read_bytes() == payload
            protocol = cartridge(rom, b'SRAM_V', second=0x5A, write=0xA5, base=0x0E000000 + offset)
            second, second_output = run(protocol, first_output)
            passed(second)
            assert first_output.read_bytes() == expected
            expected[offset + 0x123] = 0xA5
            assert second_output.read_bytes() == expected and seed.read_bytes() == payload

        payload = pattern(65536)
        seed.write_bytes(payload)
        protocol = cartridge(rom, b'FLASH_V', write=None)
        loaded, output = run(protocol, seed)
        passed(loaded)
        assert output.read_bytes() == payload and seed.read_bytes() == payload
        seed.chmod(0o444)
        try:
            readonly, output = run(protocol, seed)
            passed(readonly)
            assert output.read_bytes() == payload and seed.read_bytes() == payload
        finally:
            seed.chmod(0o644)
        protocol = cartridge(rom, b'FLASH_V', identity=True)
        selected, output = run(protocol, seed, ['--flash-profile', 'sst39vf512'])
        passed(selected)
        assert 'observations=4/4 result=PASS' in selected.stdout
        assert output.read_bytes() == payload and seed.read_bytes() == payload

        # EEPROM capacities use a result-only guest here; serial transactions
        # and programming delays retain their separate native regressions.
        for marker, size in ((b'EEPROM_V', 512), (b'EEPROM_V', 8192), (b'', 131072)):
            payload = pattern(size)
            seed.write_bytes(payload)
            protocol = cartridge(rom, marker, transport=True)
            loaded, output = run(protocol, seed)
            passed(loaded)
            assert output.read_bytes() == payload and seed.read_bytes() == payload

        protocol = cartridge(rom, b'SRAM_V')
        for length in (0, 511, 512, 513, 8192, 131072):
            payload = bytes([0xA7]) * length
            seed.write_bytes(payload)
            audio = root / f'failed-{length}.pcm'
            failed, output = run(protocol, seed, ['--dump-audio', str(audio)])
            assert failed.returncode == 2 and 'frames=' not in failed.stdout
            assert not output.exists() and not audio.exists() and seed.read_bytes() == payload
        for source in (root / 'missing.sav', root):
            failed, output = run(protocol, source)
            assert failed.returncode == 2 and 'frames=' not in failed.stdout and not output.exists()
        if hasattr(os, 'mkfifo'):
            fifo = root / 'input.fifo'
            os.mkfifo(fifo)
            failed, output = run(protocol, fifo)
            assert failed.returncode == 2 and 'frames=' not in failed.stdout and not output.exists()

        # An explicit Flash profile must not silently expand to fit the input.
        seed.write_bytes(pattern(131072))
        protocol = cartridge(rom, b'FLASH_V', transport=True)
        failed, output = run(protocol, seed, ['--flash-profile', 'sst39vf512'])
        assert failed.returncode == 2 and not output.exists()
        assert seed.read_bytes() == pattern(131072)
        malformed = subprocess.run([str(runner), str(rom), '--load-save'],
                                   capture_output=True, text=True, timeout=3)
        assert malformed.returncode == 2
        assert not rom.with_suffix('.sav').exists() and not Path(str(rom) + '.save').exists()
    print(f'PASS {count} backup runs: guest SRAM round trips, Flash identity, capacities and rejected inputs')


if __name__ == '__main__':
    main()
