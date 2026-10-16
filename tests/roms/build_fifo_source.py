#!/usr/bin/env python3
"""Build the pinned FIFO base source without replacing its bundled cartridge."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True, help='Local source checkout')
    parser.add_argument('--assembler', type=Path, required=True, help='FASMARM executable')
    parser.add_argument('--output', type=Path, required=True, help='Separate private output path')
    args = parser.parse_args()
    manifest = json.loads(Path(__file__).with_name('fifo-probes.json').read_text())
    case = next(case for case in manifest['cases'] if case['name'] == 'base-source')
    source = args.directory.resolve()
    output = args.output.resolve()
    if output == source or source in output.parents:
        parser.error('The output must remain outside the source checkout')
    expected = case['sha256']
    if output.exists() and hashlib.sha256(output.read_bytes()).hexdigest() != expected:
        parser.error('The output already contains a different file')
    inputs = {}
    for name, digest in manifest['source_files'].items():
        data = (source / name).read_bytes()
        # Git checkouts may change line endings; every other source byte is pinned.
        if hashlib.sha256(data.replace(b'\r', b'')).hexdigest() != digest:
            raise SystemExit(f'FIFO source checksum mismatch: {name}')
        inputs[name] = data
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='cupid-fifo-source-', dir=output.parent) as directory:
        scratch = Path(directory)
        assert scratch.resolve().parent == output.parent
        for name, data in inputs.items():
            path = scratch / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        binary = scratch / 'fifo.gba'
        subprocess.run([str(args.assembler.resolve()), str(scratch / 'fifo_dma/fifo.asm'),
                        str(binary)], check=True, timeout=20)
        data = binary.read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise SystemExit('Rebuilt FIFO cartridge checksum mismatch')
        output.write_bytes(data)
    print(f'PASS pinned FIFO source: {len(data)} bytes, sha256={expected}')


if __name__ == '__main__':
    main()
