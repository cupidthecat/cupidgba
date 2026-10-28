#!/usr/bin/env python3
"""Observe unchanged display-memory access sweeps in SRAM."""
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

# Each tuple selects the original main menu, sub-menu and optional scroll field.
CASES = {
    "bitmap": (2, None, False),
    "text-4": (0, 0, True),
    "text-8": (0, 4, True),
    "affine-2": (1, 0, False),
    "affine-both": (1, 2, False),
    "palette-single": (5, 0, False),
    "palette-direct": (5, 9, False),
    "palette-alpha": (5, 10, False),
    "object-oam": (6, 0, False),
    "object-vram": (6, 1, False),
}


def key_events(case):
    events = []
    frame = 16

    def press(mask):
        nonlocal frame
        events.extend(("--key-event", str(frame), str(mask), "--key-event", str(frame + 2), "0"))
        frame += 4

    main, submenu, scroll = CASES[case]
    for _ in range(main):
        press(128)
    press(1)
    if submenu is not None:
        for _ in range(submenu):
            press(128)
        press(1)
    if scroll:
        press(1)
    return events


def expected_slots(case):
    # These are occupied CPU bus clocks, including the +1 acceptance phase of
    # palette, OAM and OBJ reads. The probe's fixed lead adds 138 SRAM offsets.
    if case == "bitmap":
        clocks = range(35, 1004, 4)
    elif case in ("text-4", "text-8"):
        tile_phases = (0, 4, 20) if case == "text-4" else (0, 4, 12, 20, 28)
        clocks = (base + phase for base in range(32, 993, 32) for phase in tile_phases)
    elif case == "affine-2":
        clocks = (clock for clock in range(34, 1007) if clock % 4 in (2, 3))
    elif case == "affine-both":
        clocks = range(32, 1007)
    elif case == "palette-single":
        clocks = range(47, 1004, 4)
    elif case == "palette-direct":
        clocks = ()
    elif case == "palette-alpha":
        clocks = range(49, 1006, 4)
    elif case == "object-oam":
        # Three 16-pixel objects: overlapping attribute fetches at the start,
        # followed by the scan of the remaining disabled OAM entries.
        clocks = (41, 43, 45, 59, 61, 75, 77, *range(91, 338, 2))
    elif case == "object-vram":
        clocks = range(45, 92, 2)
    else:
        raise ValueError(case)
    occupied = {clock + 138 for clock in clocks}
    return bytes(int(i in occupied) for i in range(1231))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true")
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--test", choices=CASES)
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
    for case in (args.test,) if args.test else CASES:
        with tempfile.TemporaryDirectory(prefix="cupidgba-ppu-access-") as directory:
            save = Path(directory) / "sram.bin"
            # Each probe runs 1232 samples, each synchronized to the next frame.
            command = [str(args.runner.resolve()), str(path.resolve()), "--frames", "1500",
                       "--dump-save", str(save), *key_events(case)]
            subprocess.run(command, check=True, timeout=120)
            data = save.read_bytes()
        if len(data) != 32768:
            raise SystemExit("Incomplete SRAM observer snapshot")
        expected = expected_slots(case)
        wrong = [i for i in range(1231) if data[i] != expected[i]]
        if wrong:
            raise SystemExit(f"Display-memory {case}: {len(wrong)} differing observations; first offsets {wrong[:12]}")
        print(f"PASS display-memory {case}: 1231 observations, {sum(expected)} occupied slots")


if __name__ == "__main__":
    main()
