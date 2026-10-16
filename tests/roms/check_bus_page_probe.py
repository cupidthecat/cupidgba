#!/usr/bin/env python3
"""Require every original CPU/DMA page-boundary result from a fixed input."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile
from check_console import console_text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--bios', type=Path)
    args = parser.parse_args()
    digest = hashlib.sha256(args.rom.read_bytes()).hexdigest()
    if digest != 'e4397b9d87fd8f3f9e77ef02f8bd3ba326897849fd6e832f7e805c1e1b64b8b1':
        raise SystemExit('Page-boundary cartridge checksum mismatch')
    expected = [
        'LDM 07FFFFF8: PASS 28', 'LDM 080003F8: PASS 38',
        'LDM 0801FFF0: PASS 38', 'LDM 0801FFF8: PASS 40',
        'LDM 0803FFF0: PASS 38', 'LDM 0803FFF8: PASS 40',
        'DMA INC 07FFFFF8: PASS 57', 'DMA INC 080003F8: PASS 67',
        'DMA INC 0801FFF0: PASS 67', 'DMA INC 0801FFF8: PASS 69',
        'DMA INC 0803FFF0: PASS 67', 'DMA INC 0803FFF8: PASS 69',
        'DMA DEC 07FFFFF8: PASS 63', 'DMA DEC 080003F8: PASS 68',
        'DMA DEC 0801FFF0: PASS 70', 'DMA DEC 0801FFF8: PASS 68',
        'DMA DEC 0803FFF0: PASS 70', 'DMA DEC 0803FFF8: PASS 68',
    ]
    with tempfile.TemporaryDirectory(prefix='cupidgba-page-') as directory:
        vram, io = [Path(directory) / name for name in ('vram.bin', 'io.bin')]
        command = [str(args.runner.resolve()), str(args.rom.resolve()), '--frames',
                   '400' if args.bios else '20', '--dump-vram', str(vram), '--dump-io', str(io)]
        if args.bios:
            command += ['--bios', str(args.bios.resolve())]
        subprocess.run(command, timeout=45, check=True)
        actual = console_text(vram.read_bytes(), io.read_bytes())
    print(actual)
    missing = [line for line in expected if line not in actual.splitlines()]
    if missing or 'FAIL' in actual:
        raise SystemExit(f'Page-boundary cartridge failed: missing original results: {missing}')
    print('PASS all 18 original CPU/DMA page-boundary results')


if __name__ == '__main__':
    main()
