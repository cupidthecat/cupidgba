#!/usr/bin/env python3
# @file      build_fifo_source.py
# @author    cupidthecat
# @brief     Build the pinned FIFO base source without replacing its bundled cartridge.
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
    parser.add_argument("--directory", type=Path, required=True, help="Local source checkout")
    parser.add_argument("--assembler", type=Path, required=True, help="FASMARM executable")
    parser.add_argument("--output", type=Path, required=True, help="Separate private output path")
    args = parser.parse_args()
    manifest = json.loads(Path(__file__).with_name("fifo-probes.json").read_text())
    case = next(case for case in manifest["cases"] if case["name"] == "base-source")
    try:
        size = build(args.directory, args.assembler, args.output, manifest["source_files"], case)
    except ValueError as error:
        parser.error(str(error))
    print(f"PASS pinned FIFO source: {size} bytes, sha256={case['sha256']}")


def build(source, assembler, output, source_files, case):
    """Assemble a verified private copy and publish only a complete matching output."""
    source = source.resolve()
    output = output.resolve()
    if output == source or source in output.parents:
        raise ValueError("The output must remain outside the source checkout")
    expected = case["sha256"]
    if output.exists() and hashlib.sha256(output.read_bytes()).hexdigest() != expected:
        raise ValueError("The output already contains a different file")
    inputs = {}
    for name, digest in source_files.items():
        data = (source / name).read_bytes()
        # Git checkouts may change line endings; every other source byte is pinned.
        if hashlib.sha256(data.replace(b"\r", b"")).hexdigest() != digest:
            raise ValueError(f"FIFO source checksum mismatch: {name}")
        inputs[name] = data
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="cupid-fifo-source-", dir=output.parent) as directory:
        scratch = Path(directory)
        assert scratch.resolve().parent == output.parent
        for name, data in inputs.items():
            path = scratch / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        binary = scratch / "fifo.gba"
        subprocess.run(
            [str(assembler.resolve()), str(scratch / "fifo_dma/fifo.asm"), str(binary)],
            check=True,
            timeout=20,
        )
        data = binary.read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError("Rebuilt FIFO cartridge checksum mismatch")
        binary.replace(output)
    return len(data)


if __name__ == "__main__":
    main()
