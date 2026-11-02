#!/usr/bin/env python3
# @file check_key_script.py
# @brief Observe scripted buttons in ARM guests and reject invalid files before execution.
"""Observe scripted buttons and reject invalid files before execution."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

from check_runner_frames import cartridge


def early_cartridge():
    # Read KEYINPUT before a video frame, then complete an idle branch.
    words = (0xE3A00301, 0xE2800C01, 0xE2800030, 0xE1D010B0,
             0xE3A02402, 0xE5821000, 0xE3A0C000, 0xEAFFFFFE)
    return struct.pack("<I", 0xEA00002E) + bytes(0xBC) + struct.pack("<8I", *words)


def valid(runner, root):
    rom, script = root / "owned.gba", root / "buttons.txt"
    outputs = [root / name for name in ("image.ppm", "ram.bin", "audio.bin")]

    def run(data, arguments, events=()):
        command = [str(runner), str(rom), "--steps", "100000", *arguments]
        for option, path in zip(("screenshot", "dump-ewram", "dump-audio"), outputs):
            command += ["--" + option, str(path)]
        if data is not None:
            script.write_bytes(data)
            command += ["--key-script", str(script)]
        for frame, keys in events:
            command += ["--key-event", str(frame), str(keys)]
        result = subprocess.run(command, capture_output=True, timeout=3)
        assert result.returncode == 0, result.stdout + result.stderr
        if data is not None:
            assert script.read_bytes() == data
        return result.stdout, tuple(path.read_bytes() for path in outputs)

    rom.write_bytes(early_cartridge())
    arguments = ["--frames", "1", "--start-address", "0x080000C0",
                 "--terminal-address", "0x080000DC", "--expect-loop-register", "12"]
    events = [(0, 1), (1, 2), (2, 4)]
    expected = run(None, arguments, events)
    assert struct.unpack_from("<I", expected[1][1])[0] == 0x3FE
    assert run(b"# frame, pressed-button mask\r\n\r\n 0\t0x1 # A\r\n1 02\r\n02 4", arguments) == expected
    # The last duplicate on frame zero must survive every allocation growth.
    assert run(b"0 0\n" * 65533 + b"0 1\n1 2\n2 4\n", arguments) == expected
    no_input = run(None, arguments)
    assert struct.unpack_from("<I", no_input[1][1])[0] == 0x3FF
    for data in (b"", b" \t# empty\n\r\n", b"0 0#" + b"x" * 251,
                 b"18446744073709551615 0x3FF\n",
                 (b"#" + b"x" * 254 + b"\n") * 65536):
        assert run(data, arguments) == no_input

    # A change on frame one reaches DMA while another DMA holds the CPU.
    rom.write_bytes(cartridge())
    for arguments, samples in ((["--frames", "2"], (0x3FD, 0x0101)),
                               (["--frames", "3", "--expect-loop-register", "12"], (0x3FD, 0x3FB))):
        expected = run(None, arguments, events)
        assert struct.unpack_from("<2H", expected[1][1]) == samples
        assert run(b"0 1\n1 2\n2 4\n", arguments) == expected
    print("PASS guest input, DMA-held frame edges, file order and exact size/count limits")


def invalid(runner, root):
    rom, script = root / "owned.gba", root / "buttons.txt"
    original = early_cartridge()
    rom.write_bytes(original)
    outputs, flags = [], []
    for option in ("screenshot", "dump-vram", "dump-io", "dump-ewram",
                   "dump-save", "dump-audio", "dump-rumble"):
        path = root / option
        path.write_bytes(b"preserved output")
        outputs.append(path)
        flags += ["--" + option, str(path)]
    cases = [b"0", b"0 0 0", b"0 1024", b"2 0\n1 0", b"0 0\x00ignored\n",
             b"0 0#" + b"x" * 252, b"0 0\n" * 65537,
             (b"#" + b"x" * 254 + b"\n") * 65536 + b"#"]
    for number in (b"-1", b"+1", b"08", b"1x", b"18446744073709551616",
                   b"0x10000000000000000"):
        cases += [number + b" 0\n", b"0 " + number + b"\n"]
    commands = [(data, ["--key-script", str(script)]) for data in cases]
    commands += [(b"0 1\n", args) for args in (
        ["--key-script", str(root / "missing")],
        ["--key-script", str(root)],
        ["--key-script", str(script), "--key-script", str(script)],
        ["--key-script", str(script), "--key-event", "0", "1"],
        ["--key-event", "0", "1", "--key-script", str(script)],
        ["--key-script"],
    )]
    for data, args in commands:
        script.write_bytes(data)
        result = subprocess.run([str(runner), str(rom), *flags, *args],
                                capture_output=True, timeout=3)
        assert result.returncode == 2, (args, result.stdout, result.stderr)
        assert b"Loaded ROM" not in result.stdout and b"frames=" not in result.stdout
        assert rom.read_bytes() == original and script.read_bytes() == data
        assert all(path.read_bytes() == b"preserved output" for path in outputs)
    print(f"PASS {len(commands)} rejected scripts/options before ROM loading or output replacement")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--group", choices=("valid", "invalid"), required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cupidgba-key-script-") as folder:
        (valid if args.group == "valid" else invalid)(args.runner.resolve(), Path(folder))


if __name__ == "__main__":
    main()
