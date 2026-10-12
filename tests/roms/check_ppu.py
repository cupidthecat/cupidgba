#!/usr/bin/env python3
"""Select the original display test menus and require their complete results."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from check_console import console_text
from fetch_inputs import download_verified


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true")
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--bios", type=Path)
    parser.add_argument("--test", choices=("status", "vcount", "irq", "dma"))
    args = parser.parse_args()
    location = Path(__file__).parent
    source = json.loads((location / "console-manifest.json").read_text())
    manifest = json.loads((location / "ppu-manifest.json").read_text())
    path = args.directory / "hardware" / manifest["path"]
    if args.fetch:
        url = f'{source["repository"]}/raw/commit/{source["revision"]}/{manifest["path"]}'
        data = download_verified(url, manifest["sha256"])
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        print("Fetched verified display timing input")
        return
    if not args.runner:
        parser.error("--runner is required unless fetching inputs")
    if hashlib.sha256(path.read_bytes()).hexdigest() != manifest["sha256"]:
        raise SystemExit("Display timing input checksum mismatch")
    for case in manifest["cases"]:
        if args.test and args.test != case["name"]:
            continue
        with tempfile.TemporaryDirectory(prefix="cupidgba-ppu-") as directory:
            vram, io = [Path(directory) / name for name in ("vram.bin", "io.bin")]
            command = [str(args.runner.resolve()), str(path.resolve()), "--frames",
                       str(case["external_frames"] if args.bios else case["frames"]),
                       "--dump-vram", str(vram), "--dump-io", str(io)]
            if args.bios:
                command += ["--bios", str(args.bios.resolve())]
            menu = 300 if args.bios else 16
            for press in range(case["option"]):
                command += ["--key-event", str(menu + press * 4), "128",
                            "--key-event", str(menu + press * 4 + 2), "0"]
            select = menu + case["option"] * 4
            command += ["--key-event", str(select), "1", "--key-event", str(select + 2), "0"]
            subprocess.run(command, check=True, timeout=120)
            actual = console_text(vram.read_bytes(), io.read_bytes())
        print(actual)
        missing = [line for line in case["lines"] if line not in actual.splitlines()]
        if missing or "FAIL" in actual:
            raise SystemExit(f'Display {case["name"]}: missing expected results: {missing}')
        print(f'PASS display {case["name"]}')


if __name__ == "__main__":
    main()
