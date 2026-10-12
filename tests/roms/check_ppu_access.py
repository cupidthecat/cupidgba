#!/usr/bin/env python3
"""Observe the original bitmap-mode display-memory access sweep in SRAM."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from fetch_inputs import download_verified


ROM_PATH = "ppu/ram-access-timing/ram-access-timing.gba"
ROM_SHA256 = "b7a7b370721c771c6ccf9e1a338b475e1881890f37d5c9a672bc67e92b89de12"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true")
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--runner", type=Path)
    args = parser.parse_args()
    path = args.directory / "hardware" / ROM_PATH
    if args.fetch:
        manifest = json.loads(Path(__file__).with_name("console-manifest.json").read_text())
        url = f'{manifest["repository"]}/raw/commit/{manifest["revision"]}/{ROM_PATH}'
        data = download_verified(url, ROM_SHA256)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        print("Fetched verified display-memory sweep input")
        return
    if not args.runner:
        parser.error("--runner is required unless fetching inputs")
    if hashlib.sha256(path.read_bytes()).hexdigest() != ROM_SHA256:
        raise SystemExit("Display-memory sweep input checksum mismatch")
    with tempfile.TemporaryDirectory(prefix="cupidgba-ppu-access-") as directory:
        save = Path(directory) / "sram.bin"
        # The probe runs 1232 samples, each synchronized to the next frame.
        command = [str(args.runner.resolve()), str(path.resolve()), "--frames", "1500",
                   "--dump-save", str(save), "--key-event", "16", "128",
                   "--key-event", "18", "0", "--key-event", "20", "128",
                   "--key-event", "22", "0", "--key-event", "24", "1",
                   "--key-event", "26", "0"]
        subprocess.run(command, check=True, timeout=120)
        data = save.read_bytes()
    if len(data) != 32768:
        raise SystemExit("Incomplete SRAM observer snapshot")
    # The bitmap fetches occupy clocks 35,39,...,1003. The emitted IRQ probe's
    # fixed lead maps those collisions to offsets 173,177,...,1141 in SRAM.
    expected = bytes(int(173 <= i <= 1141 and (i - 173) % 4 == 0) for i in range(1231))
    wrong = [i for i in range(1231) if data[i] != expected[i]]
    if wrong:
        raise SystemExit(f"Display-memory sweep: {len(wrong)} differing observations; first offsets {wrong[:12]}")
    print("PASS display-memory bitmap sweep: 1231 observations, 243 occupied slots")


if __name__ == "__main__":
    main()
