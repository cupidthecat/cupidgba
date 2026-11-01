#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw a complete owned image from the upper half of a paged cartridge."""

import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

from check_peripherals import Code
from check_raster import rgb


def program():
    code = Code(False)
    code.load(4, 0x08800100)
    code.load(5, 0x02000000)
    code.load(6, 0x06000000)
    code.load(8, 0x040000D4)
    code.load(9, 0x08001000)
    code.load(10, 4096)
    code.emit(0xE584A008)  # Keep code/literals in the lower half of the aperture.
    code.emit(0xE3A07012)

    def copy(pages, words):
        code.emit(0xE5845004)
        code.load(0, pages)
        code.emit(0xE584000C)
        code.emit(0xE3A00011)
        code.emit(0xE5840000)
        code.emit(0xE5889000)
        code.emit(0xE5886004)
        code.load(0, 0x84000000 | words)
        code.emit(0xE5880008)  # DMA3 copies the mapped bytes into bitmap VRAM.

    code.label("page")
    copy(8, 1024)
    code.emit(0xE085500A)
    code.emit(0xE086600A)
    code.emit(0xE2577001)
    code.branch("page", 1)
    copy(6, 768)
    code.load(0, 0x04000000)
    code.load(1, 0x0403)
    code.emit(0xE1C010B0)
    code.load(0, 0x02000000)
    code.load(1, 0x50414745)
    for instruction in (0xE5801000, 0xE5805004, 0xE5806008, 0xE580700C):
        code.emit(instruction)
    code.label("done")
    code.branch("done", 14)
    return code.finish()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--bios", type=Path)
    args = parser.parse_args()
    colors = [
        (x & 31) | ((y & 31) << 5) | (((x ^ y) & 31) << 10) for y in range(160) for x in range(240)
    ]
    bitmap = struct.pack("<38400H", *colors)
    expected = b"P6\n240 160\n255\n" + b"".join(rgb(color) for color in colors)
    with tempfile.TemporaryDirectory(prefix="cupidgba-paged-") as temporary:
        root = Path(temporary)
        rom, vram, ram, ppm = (
            root / name for name in ("owned.gba", "vram.bin", "ram.bin", "frame.ppm")
        )
        header = bytearray(4096)
        header[0xAC:0xB0] = b"MOWN"
        struct.pack_into("<I", header, 0, 0xEA00007E)
        data = program()
        if len(data) > 4096 - 512:
            raise SystemExit("Owned program overlaps its mapping window")
        header[512 : 512 + len(data)] = data
        with rom.open("wb") as file:
            file.write(header)
            file.seek(0x02000000)
            file.write(bitmap)
            file.seek(0x04000000 - 1)
            file.write(b"\0")
        command = [
            str(args.runner.resolve()),
            str(rom),
            "--start-address",
            "0x08000200",
            "--frames",
            "4",
            "--steps",
            "1500000",
            "--dump-vram",
            str(vram),
            "--dump-ewram",
            str(ram),
            "--screenshot",
            str(ppm),
        ]
        if args.bios:
            command += ["--bios", str(args.bios.resolve())]
        result = subprocess.run(command, capture_output=True, text=True, timeout=20)
        if result.returncode or not any(
            line.startswith("frames=4 ") for line in result.stdout.splitlines()
        ):
            raise SystemExit(
                f"Paged cartridge did not finish four presentations:\n{result.stdout}\n{result.stderr}"
            )
        memory = ram.read_bytes()
        if len(memory) != 0x40000 or memory[:16] != struct.pack(
            "<4I", 0x50414745, 0x02012000, 0x06012000, 0
        ):
            raise SystemExit("Paged cartridge did not complete every mapping/DMA iteration")
        if vram.read_bytes() != bitmap + bytes(0x18000 - len(bitmap)):
            raise SystemExit("Paged cartridge bitmap differs in complete VRAM")
        if ppm.read_bytes() != expected:
            raise SystemExit("Paged cartridge differs in the complete rendered image")
    print("PASS paged cartridge: nineteen mappings/DMA copies, complete VRAM and 38,400 pixels")


if __name__ == "__main__":
    main()
