#!/usr/bin/env python3
# @file      check_irq_probe.py
# @author    cupidthecat
# @brief     Check original IRQ comparisons at their actual issued instruction sites.
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
"""Check original IRQ comparisons at their actual issued instruction sites."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from fetch_inputs import download_verified


def fetch_cases(directory, manifest):
    """Fetch pinned inputs without overwriting an altered cached cartridge."""
    base = 'https://raw.githubusercontent.com/alyosha-tas/gba-tests/' + manifest['revision']
    inputs = [('irq-probe-LICENSE.txt', '/LICENSE', manifest['license_sha256'])]
    inputs += [(case['path'], '/irq/' + case['path'], case['sha256'])
               for case in manifest['cases']]
    # Reject conflicting cached inputs before publishing any new files.
    for name, _, digest in inputs:
        path = directory / name
        if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError('Existing IRQ input checksum mismatch: ' + name)
    directory.mkdir(parents=True, exist_ok=True)
    for name, remote, digest in inputs:
        path = directory / name
        if path.exists():
            continue
        data = download_verified(base + remote, digest)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--runner', type=Path)
    parser.add_argument('--bios', type=Path)
    parser.add_argument('--test')
    parser.add_argument('--fetch', action='store_true')
    args = parser.parse_args()
    manifest = json.loads(Path(__file__).with_name('irq-probes.json').read_text())
    if args.fetch:
        try:
            fetch_cases(args.directory, manifest)
        except ValueError as error:
            parser.error(str(error))
        return
    if not args.runner or not args.bios or not args.test:
        parser.error('--runner, --bios and --test are required when not fetching')
    cases = manifest['cases']
    case = next((case for case in cases if case['name'] == args.test), None)
    if case is None:
        parser.error('Unknown IRQ cartridge')
    data = (args.directory / case['path']).read_bytes()
    if hashlib.sha256(data).hexdigest() != case['sha256']:
        raise SystemExit('IRQ cartridge checksum mismatch')
    checkpoint = int(case['checkpoint'], 16) - 0x08000000
    if data[checkpoint:checkpoint+16] != struct.pack('<4I',0xE92D0003,0xE3A00301,0xE5901004,0xE3110001):
        raise SystemExit('IRQ checkpoint does not precede the original display routine')
    command = [str(args.runner.resolve()), 'probe', str((args.directory / case['path']).resolve()),
               str(args.bios.resolve()), case['checkpoint'], case['terminal']]
    for guard in case['guards']:
        if guard['mode'] not in ('arm','thumb'):
            raise SystemExit('Unknown IRQ comparison mode')
        width = 2 if guard['mode'] == 'thumb' else 4
        offset = int(guard['address'],16) - 0x08000000
        if data[offset:offset+width] != int(guard['instruction'],16).to_bytes(width,'little'):
            raise SystemExit('IRQ observation does not match its original instruction')
        if 'continuation' in guard and data[offset+2:offset+4] != int(guard['continuation'],16).to_bytes(2,'little'):
            raise SystemExit('IRQ wide guard continuation mismatch')
        command += [guard['address'], '1' if width == 2 else '0', guard['instruction'],str(guard['zero'])]
    result = subprocess.run(command,capture_output=True,text=True,timeout=90)
    print(result.stdout,end=''); print(result.stderr,end='')
    count = len(case['guards'])
    expected = f'checkpoint=yes comparisons={count}/{count} terminal=yes r12=0 result=PASS'
    if result.returncode or expected not in result.stdout.splitlines():
        raise SystemExit('IRQ cartridge did not satisfy every original comparison and issued terminal')
    print(f"PASS unchanged IRQ cartridge: {args.test}")


if __name__ == '__main__':
    main()
