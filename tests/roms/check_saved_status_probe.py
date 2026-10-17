#!/usr/bin/env python3
"""Require every unchanged saved-status comparison and both issued result boundaries."""
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
    args = parser.parse_args()
    case = json.loads(Path(__file__).with_name('saved-status-probe.json').read_text(encoding='utf-8'))
    rom = args.directory / case['path']
    data = rom.read_bytes()
    if len(data) != case['bytes'] or hashlib.sha256(data).hexdigest() != case['sha256']:
        raise SystemExit('Saved-status cartridge checksum mismatch')
    checkpoint = int(case['checkpoint'], 16) - 0x08000000
    if data[checkpoint:checkpoint + 16] != struct.pack('<4I', 0xE92D0003, 0xE3A00301, 0xE5901004, 0xE3110001):
        raise SystemExit('Result checkpoint does not precede the original display routine')
    terminal = int(case['terminal'], 16) - 0x08000000
    if data[terminal:terminal + 4] != struct.pack('<I', 0xEAFFFFFE):
        raise SystemExit('Original terminal instruction mismatch')
    command = [str(args.runner.resolve()), str(rom.resolve()), str(args.bios.resolve()) if args.bios else '-',
               case['checkpoint'], case['terminal']]
    observations = case['observations']
    if len(observations) != 5:
        raise SystemExit('Saved-status cartridge must retain all five comparisons')
    expected = []
    for index, observation in enumerate(observations):
        address = int(observation['address'], 16)
        instruction = int(observation['instruction'], 16)
        offset = address - 0x08000000
        if observation['mode'] != 'arm' or offset < 0 or data[offset:offset + 4] != instruction.to_bytes(4, 'little'):
            raise SystemExit('Comparison does not match its original ARM instruction')
        command += [observation['address'], observation['instruction'], observation['privilege'],
                    str(observation['register']), observation['value']]
        value = int(observation['value'], 16)
        expected.append(f"issue={index} pc={address:08x} mode=arm privilege={int(observation['privilege'], 16):02x} "
                        f"opcode={instruction:08x} r{observation['register']}={value:08x} expected={value:08x} result=PASS")
    for index, (address, opcode) in enumerate(((case['checkpoint'], 0xE92D0003), (case['terminal'], 0xEAFFFFFE)), 5):
        expected.append(f'issue={index} pc={int(address, 16):08x} mode=arm privilege=1f '
                        f'opcode={opcode:08x} r12=00000000 expected=00000000 result=PASS')
    result = subprocess.run(command, text=True, capture_output=True, timeout=90)
    print(result.stdout, end='')
    if result.stderr:
        print(result.stderr, end='')
    lines = result.stdout.splitlines()
    if (result.returncode or [line for line in lines if line.startswith('issue=')] != expected
            or 'issues=7/7 checkpoint=yes terminal=yes result=PASS' not in lines):
        raise SystemExit('Saved-status cartridge did not satisfy every original issued comparison and zero-result boundary')
    print('PASS unchanged saved-status cartridge: five comparisons and both issued zero-result boundaries')


if __name__ == '__main__':
    main()
