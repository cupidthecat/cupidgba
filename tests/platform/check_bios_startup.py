#!/usr/bin/env python3
"""Verify explicit BIOS selection and failure before startup."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    choice = parser.add_mutually_exclusive_group(required=True)
    choice.add_argument("--frontend", type=Path)
    choice.add_argument("--runner", type=Path)
    args = parser.parse_args()
    executable = (args.frontend or args.runner).resolve()
    with tempfile.TemporaryDirectory(prefix="cupidgba-bios-startup-") as folder:
        root = Path(folder)
        rom = root / "probe.gba"
        rom.write_bytes(struct.pack("<I", 0xEAFFFFFE) + bytes(252))
        save = rom.with_suffix(".sav")
        sentinel = bytes((i * 13 + 17) & 255 for i in range(32768))
        save.write_bytes(sentinel)
        valid = root / "valid.bin"
        valid.write_bytes(struct.pack("<IIII", 0xE59FF000, 0xE1A00000, 0x08000000, 0xE1A00000) + bytes(16384 - 16))
        invalid = [root / "missing.bin", root]
        for size in [0, 1, 16383, 16385, 32768]:
            file = root / f"invalid-{size}.bin"
            file.write_bytes(bytes(size))
            invalid.append(file)
        env = dict(os.environ, SDL_VIDEODRIVER="cupidgba-invalid-video-driver", SDL_AUDIODRIVER="dummy")

        def run(bios):
            command = [str(executable), str(rom)]
            if args.frontend:
                if bios is not None:
                    command += ["-b", str(bios)]
            else:
                command += ["--frames", "1", "--steps", "1500000"]
                if bios is not None:
                    command += ["--bios", str(bios)]
            result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=3)
            if save.read_bytes() != sentinel:
                raise SystemExit("BIOS selection changed the existing save")
            return result

        for bios in invalid:
            result = run(bios)
            if not result.returncode or "BIOS" not in result.stderr:
                raise SystemExit(f"Invalid BIOS was not rejected: {result.stdout}\n{result.stderr}")
            if "SDL" in result.stderr or "CupidGBA started" in result.stdout or "frames=" in result.stdout or "Loaded save" in result.stdout:
                raise SystemExit(f"Invalid BIOS reached startup: {result.stdout}\n{result.stderr}")
            if args.frontend and (result.returncode != 1 or "Error: Failed to load BIOS" not in result.stderr):
                raise SystemExit("Explicit BIOS failure did not stop frontend startup")

        for bios in [None, valid]:
            result = run(bios)
            if args.frontend:
                if result.returncode != 1 or "SDL" not in result.stderr or "Error: Failed to load BIOS" in result.stderr:
                    raise SystemExit(f"Valid BIOS selection did not reach video setup: {result.stdout}\n{result.stderr}")
            elif result.returncode or "frames=1 " not in result.stdout:
                raise SystemExit(f"Valid BIOS selection did not execute: {result.stdout}\n{result.stderr}")
    print("PASS explicit BIOS validation and startup boundary")


if __name__ == "__main__":
    main()
