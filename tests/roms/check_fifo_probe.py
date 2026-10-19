#!/usr/bin/env python3
"""Require the issued comparisons and both original results of a pinned cartridge."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument('--directory', type=Path)
    inputs.add_argument('--rom', type=Path)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--bios', type=Path)
    parser.add_argument('--test', required=True)
    args = parser.parse_args()
    cases = json.loads(Path(__file__).with_name('fifo-probes.json').read_text())['cases']
    case = next((case for case in cases if case['name'] == args.test), None)
    if case is None:
        parser.error('Unknown FIFO cartridge')
    rom = args.rom if args.rom else args.directory / case['path']
    data = rom.read_bytes()
    if hashlib.sha256(data).hexdigest() != case['sha256']:
        raise SystemExit('FIFO cartridge checksum mismatch')
    checkpoint = int(case['checkpoint'], 16)
    offset = checkpoint - 0x08000000
    if data[offset:offset + 16] != struct.pack('<4I', 0xE92D0003, 0xE3A00301, 0xE5901004, 0xE3110001):
        raise SystemExit('Result checkpoint does not precede the original display routine')
    command = [str(args.runner.resolve()), str(rom.resolve()), '--frames', '360', '--steps', '150000000',
               '--expect-loop-register', '12', '--terminal-address', case['terminal'],
               '--checkpoint-address', case['checkpoint']]
    observations = case['observations']
    for observation in observations:
        address = int(observation['address'], 16)
        instruction = int(observation['instruction'], 16)
        offset = address - 0x08000000
        if observation['mode'] != 'arm' or offset < 0 or offset + 4 > len(data) or \
                data[offset:offset + 4] != struct.pack('<I', instruction):
            raise SystemExit('Comparison observation does not match its pinned instruction')
        if instruction & 0xFDF0F000 != 0xE1500000 or observation['register'] != (instruction >> 16) & 15:
            raise SystemExit('Comparison observation does not select the original CMP operand')
        value = int(observation['value'], 16)
        if instruction & 0x02000000:
            immediate = instruction & 255
            rotate = ((instruction >> 8) & 15) * 2
            expected = ((immediate >> rotate) | (immediate << (32 - rotate if rotate else 0))) & 0xFFFFFFFF
            if value != expected:
                raise SystemExit('Comparison observation changes the encoded immediate')
        else:
            literal = int(observation['literal'], 16) - 0x08000000
            if instruction & 0xFFF != 1 or literal < 0 or literal + 4 > len(data) or \
                    data[literal:literal + 4] != struct.pack('<I', value):
                raise SystemExit('Comparison observation changes the original expected literal')
        command += ['--expect-register-at' if instruction & 0x02000000 else '--expect-register-pair-at',
                    observation['address'], str(observation['register']), observation['value']]
        if not instruction & 0x02000000:
            # Require the live expected-value operand too. A different literal
            # or a bypassed failure write cannot turn a failed CMP into a pass.
            command += ['1', observation['value']]
    if args.bios:
        command += ['--bios', str(args.bios.resolve())]
    result = subprocess.run(command, text=True, capture_output=True, timeout=90)
    print(result.stdout, end='')
    if result.stderr:
        print(result.stderr, end='')
    lines = result.stdout.splitlines()
    if result.returncode or 'checkpoint=yes r12=0 result=PASS' not in lines or 'terminal=yes r12=0 result=PASS' not in lines:
        raise SystemExit('FIFO cartridge did not pass both original result boundaries')
    if observations:
        expected = []
        for index, observation in enumerate(observations):
            value = int(observation['value'], 16)
            instruction = int(observation['instruction'], 16)
            second = '' if instruction & 0x02000000 else f' r1={value:08x} expected={value:08x}'
            expected.append(f"observation={index} pc={int(observation['address'], 16):08x} "
                            f"mode=arm opcode={instruction:08x} "
                            f"r{observation['register']}={value:08x} expected={value:08x}{second} result=PASS")
        actual = [line for line in lines if line.startswith('observation=')]
        if actual != expected or f'observations={len(observations)}/{len(observations)} result=PASS' not in lines:
            raise SystemExit('FIFO cartridge did not satisfy every original comparison observation')
    print(f"PASS unchanged FIFO cartridge: {args.test}")


if __name__ == '__main__':
    main()
