#!/usr/bin/env python3
# @file check_save_selection.py
# @brief Require explicit backup selection, guest transactions and independent reloads.
"""Require explicit backup selection, guest transactions and independent reloads."""

# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "roms"))
from save_selection_driver import cartridge

SIZES = {
    "sram": 32768,
    "sram512": 65536,
    "flash512": 65536,
    "flash1m": 131072,
    "sst39vf512": 65536,
    "eeprom": 8192,
    "eeprom512": 512,
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--frontend", type=Path)
    parser.add_argument("--bios", type=Path)
    args = parser.parse_args()
    runner = args.runner.resolve()
    count = 0
    with tempfile.TemporaryDirectory(prefix="cupidgba-save-selection-") as directory:
        root = Path(directory)
        rom = root / "driver.gba"
        ram, output = root / "ram.bin", root / "output.sav"
        for kind, size in SIZES.items():
            seed = root / f"{kind}.sav"
            expected = bytearray([255]) * size
            for read_only in (False, True):
                image, terminal, offset, payload, observed = cartridge(kind, read_only=read_only)
                # Deliberately misleading detection must not override an
                # explicit device choice made before battery restoration.
                image = bytearray(image)
                marker = b"EEPROM_V" if kind.startswith("flash") else b"FLASH1M_V"
                image[0x1800 : 0x1800 + len(marker)] = marker
                rom.write_bytes(image)
                command = [
                    str(runner),
                    str(rom),
                    "--save-type",
                    kind,
                    "--frames",
                    "3",
                    "--steps",
                    "200000",
                    "--start-address",
                    "0x080000c0",
                    "--terminal-address",
                    hex(terminal),
                    "--expect-loop-register",
                    "12",
                    "--dump-save",
                    str(output),
                    "--dump-ewram",
                    str(ram),
                ]
                if args.bios:
                    command += ["--bios", str(args.bios.resolve())]
                if read_only:
                    command += ["--load-save", str(seed)]
                result = subprocess.run(command, capture_output=True, text=True, timeout=3)
                count += 1
                assert result.returncode == 0, (kind, read_only, result.stdout, result.stderr)
                assert "terminal=yes r12=0 result=PASS" in result.stdout
                expected[offset : offset + len(payload)] = payload
                assert output.read_bytes() == expected, (kind, "complete backup")
                assert ram.read_bytes() == observed + bytes(262144 - len(observed)), (
                    kind,
                    "guest readback",
                )
                if read_only:
                    assert seed.read_bytes() == expected
                else:
                    seed.write_bytes(output.read_bytes())
            # A forced smaller family/capacity must not be inferred from input bytes.
            seed.write_bytes(bytes([0x69]) * (size * 2))
            before = {path: path.read_bytes() for path in (rom, ram, output, seed)}
            result = subprocess.run(command, capture_output=True, text=True, timeout=3)
            count += 1
            assert result.returncode == 2 and "frames=" not in result.stdout
            assert all(path.read_bytes() == data for path, data in before.items())

        # Explicit auto preserves signature detection; none leaves the backup
        # bus inactive. Both are distinct from an unknown spelling.
        image, terminal, offset, payload, observed = cartridge("sram")
        image = bytearray(image)
        image[0x1800:0x1806] = b"SRAM_V"
        rom.write_bytes(image)
        for flags in ([], ["--save-type", "auto"], ["-S", "auto"], ["-S", "sram"]):
            result = subprocess.run(
                [
                    str(runner),
                    str(rom),
                    "--start-address",
                    "0x080000c0",
                    "--frames",
                    "1",
                    "--steps",
                    "10000",
                    "--expect-loop-register",
                    "12",
                    "--terminal-address",
                    hex(terminal),
                    "--dump-save",
                    str(output),
                    *flags,
                ],
                capture_output=True,
                text=True,
                timeout=3,
            )
            count += 1
            expected = bytearray([255]) * 32768
            expected[offset : offset + len(payload)] = payload
            assert result.returncode == 0 and output.read_bytes() == expected

        image, terminal, _, _, _ = cartridge("sram", read_only=True)
        image = bytearray(image)
        image[0x1800:0x1806] = b"SRAM_V"
        rom.write_bytes(image)
        before = output.read_bytes()
        result = subprocess.run(
            [
                str(runner),
                str(rom),
                "--save-type",
                "none",
                "--start-address",
                "0x080000c0",
                "--frames",
                "1",
                "--steps",
                "10000",
                "--expect-loop-register",
                "12",
                "--terminal-address",
                hex(terminal),
                "--dump-save",
                str(output),
            ],
            capture_output=True,
            text=True,
            timeout=3,
        )
        count += 1
        assert result.returncode == 2 and "terminal=yes r12=0 result=PASS" in result.stdout
        assert output.read_bytes() == before

        # The old explicit Flash spelling and the shared selector must produce
        # identical full batteries, including when combined in either order.
        image, terminal, offset, payload, observed = cartridge("sst39vf512")
        rom.write_bytes(image)
        variants = (
            ["--flash-profile", "sst39vf512"],
            ["--save-type", "sst39vf512"],
            ["--save-type", "flash512", "--flash-profile", "sst39vf512"],
            ["--flash-profile", "sst39vf512", "--save-type", "flash512"],
        )
        for flags in variants:
            result = subprocess.run(
                [
                    str(runner),
                    str(rom),
                    "--start-address",
                    "0x080000c0",
                    "--frames",
                    "1",
                    "--steps",
                    "10000",
                    "--expect-loop-register",
                    "12",
                    "--terminal-address",
                    hex(terminal),
                    "--dump-save",
                    str(output),
                    *flags,
                ],
                capture_output=True,
                text=True,
                timeout=3,
            )
            count += 1
            expected = bytearray([255]) * 65536
            expected[offset : offset + len(payload)] = payload
            assert result.returncode == 0 and output.read_bytes() == expected

        # Parsing errors must happen before loading the ROM or opening outputs.
        for flags in (
            ["--save-type", "bad"],
            ["--save-type"],
            ["-S", "SRAM"],
            ["--save-type", "sram", "--flash-profile", "sst39vf512"],
            ["--flash-profile", "sst39vf512", "--save-type", "eeprom"],
        ):
            before = {path: path.read_bytes() for path in (rom, ram, output)}
            result = subprocess.run(
                [
                    str(runner),
                    str(rom),
                    "--dump-save",
                    str(output),
                    "--dump-ewram",
                    str(ram),
                    *flags,
                ],
                capture_output=True,
                text=True,
                timeout=3,
            )
            count += 1
            assert result.returncode == 2 and "frames=" not in result.stdout
            assert all(path.read_bytes() == data for path, data in before.items())

        if args.frontend:
            env = dict(
                os.environ,
                SDL_VIDEODRIVER="dummy",
                SDL_AUDIODRIVER="dummy",
                SDL_RENDER_DRIVER="software",
            )
            for kind, size in SIZES.items():
                image, _, offset, payload, _ = cartridge(kind)
                rom.write_bytes(image)
                expected = bytearray([255]) * size
                expected[offset : offset + len(payload)] = payload
                for local in (False, True):
                    flags = ["--link", str(rom)] if local else []
                    paths = (
                        [rom.with_suffix(f".p{i}.sav") for i in (1, 2)]
                        if local
                        else [rom.with_suffix(".sav")]
                    )
                    for path in paths:
                        path.write_bytes(bytes([255]) * size)
                    command = [
                        str(args.frontend.resolve()),
                        str(rom),
                        "-S",
                        kind,
                        "--frames",
                        "2",
                        "-s",
                        "1",
                        *flags,
                    ]
                    result = subprocess.run(
                        command, env=env, capture_output=True, text=True, timeout=3
                    )
                    count += 1
                    assert result.returncode == 0, (kind, local, result.stdout, result.stderr)
                    assert all(path.read_bytes() == expected for path in paths), (kind, local)
            for local in (False, True):
                before = {path: path.read_bytes() for path in root.iterdir() if path.is_file()}
                flags = ["--link", str(rom)] if local else []
                result = subprocess.run(
                    [
                        str(args.frontend.resolve()),
                        str(rom),
                        "-S",
                        "invalid",
                        "--frames",
                        "1",
                        *flags,
                    ],
                    env=env,
                    capture_output=True,
                    text=True,
                    timeout=3,
                )
                count += 1
                assert result.returncode == 1 and "Unknown save type" in result.stderr
                assert "CupidGBA started" not in result.stdout
                assert all(path.read_bytes() == data for path, data in before.items())
        assert not list(root.glob("*.tmp.*"))
    print(
        f"PASS {count} backup selection runs: guest writes/readback, full files, reloads and input rejection"
    )


if __name__ == "__main__":
    main()
