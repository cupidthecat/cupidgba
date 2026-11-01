#!/usr/bin/env python3
"""Check every original record, comparison and branch of a pinned CPU corpus."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile

from fetch_inputs import download_verified


def check_existing(path, digest):
    if not path.is_file():
        raise SystemExit(f'CPU corpus input must be a regular file: {path}')
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise SystemExit(f'Existing CPU corpus input differs from pinned data: {path}')


def store_input(path, data, digest):
    if path.exists() or path.is_symlink():
        check_existing(path, digest)
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open('xb') as output:
            output.write(data)
    except FileExistsError:
        # Another fetch may have published this input after the first check.
        check_existing(path, digest)


def verify_input(data, manifest, case):
    if len(data) != manifest['bytes'] or hashlib.sha256(data).hexdigest() != case['sha256']:
        raise SystemExit('CPU corpus cartridge length or checksum mismatch')
    for site in manifest['control_words']:
        address = int(site['address'], 16)
        offset = address - 0x08000000
        if offset < 0 or data[offset:offset + 4] != int(site['value'], 16).to_bytes(4, 'little'):
            raise SystemExit('CPU corpus layout does not match the pinned program')
    count_offset = manifest['table_offset'] - 4
    if count_offset < 0 or count_offset + 4 > len(data):
        raise SystemExit('CPU corpus record count address is outside the input')
    if struct.unpack_from('<I', data, count_offset)[0] != manifest['cases_per_rom']:
        raise SystemExit('CPU corpus original record count mismatch')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fetch', action='store_true')
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--runner', type=Path)
    parser.add_argument('--bios', type=Path)
    parser.add_argument('--test')
    args = parser.parse_args()
    manifest = json.loads(Path(__file__).with_name('cpu-corpus.json').read_text())
    cases = manifest['cases']
    if args.test:
        cases = [case for case in cases if case['name'] == args.test]
    if not cases:
        parser.error('Unknown CPU corpus cartridge')
    if args.fetch:
        source = f'{manifest["source"]}/{manifest["revision"]}'
        license_data = download_verified(f'{source}/LICENSE', manifest['license_sha256'])
        store_input(args.directory / 'cpu-corpus-LICENSE.txt', license_data, manifest['license_sha256'])
        for case in cases:
            data = download_verified(f'{source}/{case["path"]}', case['sha256'])
            store_input(args.directory / case['path'], data, case['sha256'])
            print(f'Fetched verified CPU corpus input: {case["name"]}')
        return
    if not args.runner or not args.test or len(cases) != 1:
        parser.error('--runner and --test are required unless fetching inputs')
    case = cases[0]
    rom = args.directory / case['path']
    if not rom.is_file():
        raise SystemExit('CPU corpus input must be a regular file')
    data = rom.read_bytes()
    verify_input(data, manifest, case)
    with tempfile.TemporaryDirectory(prefix='cupidgba-cpu-corpus-') as directory:
        output = Path(directory) / 'complete.ewram'
        command = [str(args.runner.resolve()), str(rom.resolve()), str(output)]
        if args.bios:
            command.append(str(args.bios.resolve()))
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=90)
        except subprocess.TimeoutExpired as error:
            for block in (error.stdout, error.stderr):
                if block:
                    print(block.decode(errors='replace') if isinstance(block, bytes) else block, end='')
            raise SystemExit('CPU corpus process guard expired; incomplete')
        print(result.stdout, end='')
        print(result.stderr, end='')
        summaries = [line for line in result.stdout.splitlines() if line.startswith('cases=')]
        pattern = (r'cases=10000/10000 loads=10000 comparisons=30000/30000 '
                   r'branches=30000/30000 terminal=yes frames=(\d+) steps=(\d+) '
                   r'cycles=(\d+) result=PASS')
        match = re.fullmatch(pattern, summaries[0]) if len(summaries) == 1 else None
        if result.returncode or not match or any(line.startswith('FAIL ') for line in result.stdout.splitlines()):
            raise SystemExit('CPU corpus did not satisfy all original records, comparisons, branches and terminal')
        frames, steps, cycles = map(int, match.groups())
        if frames >= 1500 or steps >= 150000000 or cycles >= 1500 * 280896:
            raise SystemExit('CPU corpus exceeded its execution guard')
        if not output.is_file() or output.stat().st_size != 262144:
            raise SystemExit('CPU corpus complete EWRAM output is missing')
    print(f'PASS unchanged CPU corpus: {case["name"]}, 10000 records and 60000 issued comparison/branch observations')


if __name__ == '__main__':
    main()
