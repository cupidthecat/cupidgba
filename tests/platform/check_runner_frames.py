#!/usr/bin/env python3
# @file check_runner_frames.py
# @brief Check frame deadlines and immutable screenshots across a long DMA wait.
"""Check frame deadlines and immutable screenshots across a long DMA wait."""

# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
from pathlib import Path
import re
import struct
import subprocess
import tempfile


def cartridge():
    # Owned ARM code: mode 4, one palette index, and a short VRAM fill.
    words = [
        0xE3A00301,  # mov r0, #0x4000000
        0xE3A01000,  # mov r1, #0
        0xE5801020,  # str r1, [r0, #0x20]
        0xE5801024,  # str r1, [r0, #0x24]
        0xE5801028,  # str r1, [r0, #0x28]
        0xE580102C,  # str r1, [r0, #0x2c]
        0xE3A01C01,  # mov r1, #0x100
        0xE1C012B0,  # strh r1, [r0, #0x20]
        0xE1C012B6,  # strh r1, [r0, #0x26]
        0xE3A01B01,  # mov r1, #0x400
        0xE2811004,  # add r1, r1, #4
        0xE1C010B0,  # strh r1, [r0]
        0xE3A02405,  # mov r2, #0x5000000
        0xE3A0101F,  # mov r1, #0x1f
        0xE1C210B2,  # strh r1, [r2, #2]
        0xE3A03403,  # mov r3, #0x3000000
        0xE3A01401,  # mov r1, #0x1000000
        0xE2811801,  # add r1, r1, #0x10000
        0xE2811C01,  # add r1, r1, #0x100
        0xE2811001,  # add r1, r1, #1
        0xE5831000,  # str r1, [r3]
        0xE58030D4,  # str r3, [r0, #0xd4]
        0xE3A01406,  # mov r1, #0x6000000
        0xE58010D8,  # str r1, [r0, #0xd8]
        0xE3A01D96,  # mov r1, #0x2580
        0xE1C01DBC,  # strh r1, [r0, #0xdc]
        0xE3A01C85,  # mov r1, #0x8500
        0xE1C01DBE,  # strh r1, [r0, #0xde]
        # DMA0 changes the palette every VBlank while DMA3 holds the CPU.
        0xE3A01E3E,  # mov r1, #0x3e0
        0xE1C310B4,  # strh r1, [r3, #4]
        0xE3A01B1F,  # mov r1, #0x7c00
        0xE1C310B6,  # strh r1, [r3, #6]
        0xE3A0101F,  # mov r1, #0x1f
        0xE1C310B8,  # strh r1, [r3, #8]
        0xE3A01C7F,  # mov r1, #0x7f00
        0xE28110FF,  # add r1, r1, #0xff
        0xE1C310BA,  # strh r1, [r3, #0xa]
        0xE2831004,  # add r1, r3, #4
        0xE58010B0,  # str r1, [r0, #0xb0]
        0xE2821002,  # add r1, r2, #2
        0xE58010B4,  # str r1, [r0, #0xb4]
        0xE3A01001,  # mov r1, #1
        0xE1C01BB8,  # strh r1, [r0, #0xb8]
        0xE3A01C92,  # mov r1, #0x9200
        0xE2811040,  # add r1, r1, #0x40
        0xE1C01BBA,  # strh r1, [r0, #0xba]
        # DMA1 samples input into EWRAM already written by the ongoing DMA3 fill.
        0xE2801C01,  # add r1, r0, #0x100
        0xE2811030,  # add r1, r1, #0x30
        0xE58010BC,  # str r1, [r0, #0xbc]
        0xE3A01402,  # mov r1, #0x2000000
        0xE58010C0,  # str r1, [r0, #0xc0]
        0xE3A01001,  # mov r1, #1
        0xE1C01CB4,  # strh r1, [r0, #0xc4]
        0xE3A01C93,  # mov r1, #0x9300
        0xE1C01CB6,  # strh r1, [r0, #0xc6]
        # Full EWRAM word fill: two frame completions inside one CPU return.
        0xE3A01402,  # mov r1, #0x2000000
        0xE58010D8,  # str r1, [r0, #0xd8]
        0xE3A01000,  # mov r1, #0
        0xE1C01DBC,  # strh r1, [r0, #0xdc]
        0xE3A01C85,  # mov r1, #0x8500
        0xE1C01DBE,  # strh r1, [r0, #0xde]
        0xE3A0C000,  # mov ip, #0
        0xEAFFFFFE,  # b #0x80001b8
    ]
    return struct.pack("<I", 0xEA00002E) + bytes(0xBC) + struct.pack("<63I", *words)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    runner = parser.parse_args().runner.resolve()
    with tempfile.TemporaryDirectory(prefix="cupidgba-frames-") as folder:
        folder = Path(folder)
        rom = folder / "owned.gba"
        rom.write_bytes(cartridge())

        def run(frames, terminal=False, steps=100000, keys=()):
            image = folder / "frame.ppm"
            ram = folder / "ewram.bin"
            command = [
                str(runner),
                str(rom),
                "--frames",
                str(frames),
                "--steps",
                str(steps),
                "--screenshot",
                str(image),
                "--dump-ewram",
                str(ram),
            ]
            if terminal:
                command += ["--expect-loop-register", "12"]
            for frame, mask in keys:
                command += ["--key-event", str(frame), str(mask)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=5)
            match = re.search(
                r"^frames=(\d+) cycles=(\d+) steps=(\d+) pc=([0-9a-f]+)$",
                result.stdout,
                re.MULTILINE,
            )
            if match is None:
                raise AssertionError(result.stdout + result.stderr)
            data = image.read_bytes()
            header = b"P6\n240 160\n255\n"
            assert data.startswith(header) and len(data) == len(header) + 240 * 160 * 3
            return (
                result,
                tuple(int(value) for value in match.groups()[:3]),
                data[len(header) :],
                ram.read_bytes(),
            )

        # The second complete image is green; a coalesced ready flag captures blue instead.
        second, state, pixels, _ = run(2)
        assert second.returncode == 0 and state[0] == 2, second.stdout
        assert pixels == b"\x00\xff\x00" * (240 * 160)
        second_clock, second_steps = state[1:]
        third, state, pixels, _ = run(3)
        assert third.returncode == 0 and state[0] == 3, third.stdout
        assert pixels == b"\x00\x00\xff" * (240 * 160)
        assert state[1] - second_clock == 280896

        # The idle branch is issued before DMA releases its fetch. Its unfinished
        # return must not pass a two-frame deadline, even with a zero result register.
        late, state, pixels, _ = run(2, terminal=True)
        assert late.returncode == 1 and state == (2, second_clock, second_steps), late.stdout
        assert "terminal-issued=yes completed=no" in late.stdout
        assert "terminal=no r12=0 result=INCOMPLETE" in late.stdout
        assert pixels == b"\x00\xff\x00" * (240 * 160)

        # With enough time the terminal passes during the next visible frame.
        # Its screenshot must retain the last COMPLETE green image, not the
        # partially overwritten blue scanlines in the live output buffer.
        accepted, state, pixels, _ = run(3, terminal=True)
        assert accepted.returncode == 0 and state[0] == 2, accepted.stdout
        assert "terminal=yes r12=0 result=PASS" in accepted.stdout
        assert pixels == b"\x00\xff\x00" * (240 * 160)
        assert second_clock < state[1] < second_clock + 280896

        # A step budget still wins before presentation; a frame-zero input still
        # applies before execution. Input on the final frame is not applied late.
        short, state, pixels, _ = run(2, steps=1)
        assert short.returncode == 1 and state[0] == 0 and state[2] == 1
        assert pixels == bytes(240 * 160 * 3)
        keyed, state, pixels, ram = run(2, keys=((0, 1), (1, 2), (2, 4)))
        assert keyed.returncode == 0 and state == (2, second_clock, second_steps)
        assert pixels == b"\x00\xff\x00" * (240 * 160)
        assert len(ram) == 0x40000
        # Input one is delivered before the first VBlank DMA read. The second
        # deadline stops before its DMA can overwrite the following halfword.
        assert struct.unpack_from("<2H", ram) == (0x3FD, 0x0101)
        keyed, state, pixels, ram = run(3, terminal=True, keys=((0, 1), (1, 2), (2, 4)))
        assert keyed.returncode == 0 and state[0] == 2, keyed.stdout
        assert struct.unpack_from("<2H", ram) == (0x3FD, 0x3FB)
        assert pixels == b"\x00\xff\x00" * (240 * 160)
    print("PASS exact DMA-held frame limits, completed-frame captures, terminal and step guards")


if __name__ == "__main__":
    main()
