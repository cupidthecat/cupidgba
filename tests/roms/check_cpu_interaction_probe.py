#!/usr/bin/env python3
# @file      check_cpu_interaction_probe.py
# @author    cupidthecat
# @brief     Check the original comparisons and result boundaries of a CPU/DMA/IRQ cartridge.
# @date      2026-11-16
#
# @copyright Copyright (c) 2026 cupidthecat
#
# This file is part of CupidGBA.
#
# CupidGBA is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# CupidGBA is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with CupidGBA.  If not, see <https://gnu.org>.
"""Check the original comparisons and result boundaries of a CPU/DMA/IRQ cartridge."""
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
    cases = json.loads(Path(__file__).with_name('cpu-interaction-probes.json').read_text())['cases']
    case = next((case for case in cases if case['name'] == args.test), None)
    if case is None:
        parser.error('Unknown CPU interaction cartridge')
    rom = args.directory / case['path']
    data = rom.read_bytes()
    if hashlib.sha256(data).hexdigest() != case['sha256']:
        raise SystemExit('CPU interaction cartridge checksum mismatch')
    checkpoint = int(case['checkpoint'], 16)
    offset = checkpoint - 0x08000000
    if data[offset:offset + 16] != struct.pack('<4I', 0xE92D0003, 0xE3A00301, 0xE5901004, 0xE3110001):
        raise SystemExit('Result checkpoint does not precede the original display routine')
    command = [str(args.runner.resolve()), str(rom.resolve()), '--frames', '360', '--steps', '150000000',
               '--expect-loop-register', '12', '--terminal-address', case['terminal'],
               '--checkpoint-address', case['checkpoint']]
    observations = case['observations']
    if not observations:
        raise SystemExit('CPU interaction cartridge has no original comparison observations')
    for observation in observations:
        address = int(observation['address'], 16)
        if observation['mode'] not in ('arm', 'thumb'):
            raise SystemExit('Unknown comparison instruction mode')
        width = 2 if observation['mode'] == 'thumb' else 4
        instruction = int(observation['instruction'], 16)
        offset = address - 0x08000000
        if offset < 0 or offset + width > len(data) or data[offset:offset + width] != instruction.to_bytes(width, 'little'):
            raise SystemExit('Comparison observation does not match its pinned instruction')
        command += ['--expect-register-at', observation['address'],
                    str(observation['register']), observation['value']]
    if args.bios:
        command += ['--bios', str(args.bios.resolve())]
    result = subprocess.run(command, text=True, capture_output=True, timeout=90)
    print(result.stdout, end='')
    if result.stderr:
        print(result.stderr, end='')
    lines = result.stdout.splitlines()
    if result.returncode or 'checkpoint=yes r12=0 result=PASS' not in lines or 'terminal=yes r12=0 result=PASS' not in lines:
        raise SystemExit('CPU interaction cartridge did not pass both original result boundaries')
    if observations:
        expected = []
        for index, observation in enumerate(observations):
            value = int(observation['value'], 16)
            expected.append(f"observation={index} pc={int(observation['address'], 16):08x} "
                            f"mode={observation['mode']} opcode={int(observation['instruction'], 16):08x} "
                            f"r{observation['register']}={value:08x} expected={value:08x} result=PASS")
        actual = [line for line in lines if line.startswith('observation=')]
        if actual != expected or f'observations={len(observations)}/{len(observations)} result=PASS' not in lines:
            raise SystemExit('CPU interaction cartridge did not satisfy every original comparison observation')
    print(f"PASS unchanged CPU interaction cartridge: {args.test}")


if __name__ == '__main__':
    main()
