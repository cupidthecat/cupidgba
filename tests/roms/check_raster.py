#!/usr/bin/env python3
"""Check source-derived raster pixels from unchanged, checksum-verified inputs."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from fetch_inputs import download_verified


def rgb(color):
    # The original expectations below remain BGR555 words. Compare every host
    # bit after expanding their five-bit channels into opaque RGB output.
    return bytes(((value << 3) | (value >> 2))
                 for value in (color & 31, (color >> 5) & 31, (color >> 10) & 31))


def check_pixels(name, data):
    prefix = b"P6\n240 160\n255\n"
    if not data.startswith(prefix) or len(data) != len(prefix) + 240 * 160 * 3:
        raise SystemExit("Incomplete raster observer snapshot")
    data = data[len(prefix):]
    observations = 0

    def require(x, y, color):
        nonlocal observations
        offset = (y * 240 + x) * 3
        if data[offset:offset + 3] != rgb(color):
            raise SystemExit(f"{name}: pixel ({x},{y}) differs from {color:04x}")
        observations += 1

    if name == "green-swap":
        colors = (0x001F, 0x03E0, 0x7C00)
        for x in range(240):
            require(x, 0, colors[x % 3])
        for y in range(1, 160):
            # The DMA spans the start of the next visible line. Check both
            # stable regions without assigning an unmeasured transition edge.
            for x in range(48):
                own, adjacent = colors[x % 3], colors[(x ^ 1) % 3]
                require(x, y, (own & ~0x03E0) | (adjacent & 0x03E0))
            for x in range(160, 240):
                require(x, y, colors[x % 3])
    elif name == "affine-line-step":
        # HBlank DMA starts writing PD at 1012, then every two clocks. The
        # line step precedes the write at 1232, retaining PD=109 for that step.
        for y in range(160):
            color = ((y * 109 // 256) & 31) * 0x0421
            for x in range(240):
                require(x, y, color)
    elif name == "sprite-mosaic":
        texture = (0x53535353, 0x34444445, 0x54212143, 0x34100245,
                   0x54200143, 0x34121245, 0x54444443, 0x35353535)
        colors = (0, 0x001F, 0x03E0, 0x7C00, 0x7FFF, 0x03FF)
        for scene in range(16):
            for y in range(8):
                for x in range(8):
                    color = colors[(texture[y] >> (x * 4)) & 15]
                    require(8 + x, 1 + scene * 10 + y, color)
                    require(32 + x, 1 + scene * 10 + y, color)
        # These scenes have mosaic widths two and eight. All OBJ starts align
        # with their mosaic groups, so each group repeats its leading texel.
        for scene, width in ((1, 2), (7, 8)):
            for start in (16, 24, 40, 48, 56, 64):
                for y in range(8):
                    for x in range(8):
                        texel = x - x % width
                        require(start + x, 1 + scene * 10 + y, colors[(texture[y] >> (texel * 4)) & 15])
    else:
        raise SystemExit("Unknown raster expectation")
    print(f"PASS {name}: {observations} source-derived pixel observations")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true")
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--bios", type=Path)
    parser.add_argument("--test", choices=("green-swap", "affine-line-step", "sprite-mosaic"))
    args = parser.parse_args()
    location = Path(__file__).parent
    source = json.loads((location / "console-manifest.json").read_text())
    cases = json.loads((location / "raster-manifest.json").read_text())["cases"]
    if not args.fetch and not args.runner:
        parser.error("--runner is required unless fetching inputs")
    for case in cases:
        if args.test and case["name"] != args.test:
            continue
        path = args.directory / "hardware" / case["path"]
        if args.fetch:
            url = f'{source["repository"]}/raw/commit/{source["revision"]}/{case["path"]}'
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(download_verified(url, case["sha256"]))
            print(f'Fetched verified raster input: {case["name"]}')
            continue
        if hashlib.sha256(path.read_bytes()).hexdigest() != case["sha256"]:
            raise SystemExit(f'Raster input checksum mismatch: {case["name"]}')
        with tempfile.TemporaryDirectory(prefix="cupidgba-raster-") as directory:
            snapshot = Path(directory) / "output.ppm"
            command = [str(args.runner.resolve()), str(path.resolve()), "--frames",
                       "400" if args.bios else "120", "--screenshot", str(snapshot)]
            if args.bios:
                command += ["--bios", str(args.bios.resolve())]
            subprocess.run(command, check=True, timeout=45)
            check_pixels(case["name"], snapshot.read_bytes())


if __name__ == "__main__":
    main()
