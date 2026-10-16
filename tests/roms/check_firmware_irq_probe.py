#!/usr/bin/env python3
"""Require the original firmware-return IRQ comparisons and issued result boundaries."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--bios', type=Path)
    parser.add_argument('--test', required=True)
    args = parser.parse_args()
    manifest = json.loads(Path(__file__).with_name('firmware-irq-probes.json').read_text(encoding='utf-8'))
    case = next((c for c in manifest['cases'] if c['name'] == args.test), None)
    if not case:
        parser.error('Unknown firmware-return IRQ cartridge')
    rom = args.directory / case['path']
    data = rom.read_bytes()
    if hashlib.sha256(data).hexdigest() != case['sha256']:
        raise SystemExit('Firmware-return IRQ cartridge checksum mismatch')
    checkpoint = int(case['checkpoint'], 16) - 0x08000000
    terminal = int(case['terminal'], 16) - 0x08000000
    if data[checkpoint:checkpoint + 16] != struct.pack('<4I', 0xE92D0003, 0xE3A00301, 0xE5901004, 0xE3110001):
        raise SystemExit('Original display checkpoint mismatch')
    if data[terminal:terminal + 4] != struct.pack('<I', 0xEAFFFFFE):
        raise SystemExit('Original terminal instruction mismatch')
    command = [str(args.runner.resolve()), 'probe', str(rom.resolve()),
               str(args.bios.resolve()) if args.bios else '-', case['checkpoint'], case['terminal']]
    expected = []
    if not case['guards']:
        raise SystemExit('Original IRQ comparisons are required')
    for index, guard in enumerate(case['guards']):
        if guard['mode'] not in ('arm', 'thumb'):
            raise SystemExit('Unknown original comparison mode')
        width = 2 if guard['mode'] == 'thumb' else 4
        address = int(guard['address'], 16)
        instruction = int(guard['instruction'], 16)
        offset = address - 0x08000000
        if offset < 0 or data[offset:offset + width] != instruction.to_bytes(width, 'little'):
            raise SystemExit('Original comparison opcode mismatch')
        command += [guard['address'], '1' if width == 2 else '0', guard['instruction'], str(guard['zero'])]
        expected.append(f"guard={index} pc={address:08x} mode={guard['mode']} opcode={instruction:08x} "
                        f"z={guard['zero']} expected={guard['zero']} result=PASS")
    result = subprocess.run(command, capture_output=True, text=True, timeout=90)
    print(result.stdout, end='')
    print(result.stderr, end='')
    lines = result.stdout.splitlines()
    count = len(case['guards'])
    summary = f'checkpoint=yes comparisons={count}/{count} terminal=yes r12=0 result=PASS'
    if result.returncode or summary not in lines or [line for line in lines if line.startswith('guard=')] != expected:
        raise SystemExit('Firmware-return cartridge did not satisfy every original comparison and issued zero-result boundary')
    print(f'PASS unchanged firmware-return IRQ cartridge: {args.test}')


if __name__ == '__main__':
    main()
