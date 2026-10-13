#!/usr/bin/env python3
"""Observe source-defined signals from the unchanged local audio cartridge."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import math
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROM_SHA256 = "fb8b3a5677ae66fb80feedbfefb04c6e83e73e63c431a8a286494976113b2c03"
FRAME_CLOCKS = 280896
CASES = {"tone": (0, 240), "stereo": (1, 600), "sweep": (2, 600), "steps": (3, 650), "dynamic": (4, 700),
         "noise": (5, 450), "wave": (6, 480), "bias": (7, 760),
         "fifo": (8, 540), "silence": (9, 600)}


def window(data, start, end):
    selected = [row for row in data if start * FRAME_CLOCKS <= row[0] < end * FRAME_CLOCKS]
    if len(selected) < 1000:
        raise SystemExit("Incomplete audio observation window")
    return selected


def square(data, frequency, channel=1, amplitude=9216):
    values = {row[channel] for row in data}
    if values != {0, amplitude}:
        raise SystemExit(f"Square signal levels differ: {sorted(values)[:18]}")
    period = 128 * (131072 // frequency)
    half = period // 2
    spacing = data[1][0] - data[0][0]
    quantum = math.gcd(spacing, half)
    observations = [(row[0] % period, row[channel] == amplitude) for row in data]
    for phase in range(0, period, quantum):
        if all(((clock - phase) % period < half) == high for clock, high in observations):
            return
    raise SystemExit(f"Square signal does not match the programmed {frequency} Hz register period")


def mono(data):
    if any(left != right for _, left, right in data):
        raise SystemExit("Expected equal stereo routing")


def wave(data):
    mono(data)
    # Four-nibble words contain an ascending and descending ramp. The CPU
    # stores their low byte first, and the sound engine reads its high nibble.
    ramp = list(range(16)) + list(range(15, -1, -1))
    pattern = []
    for start in range(0, 32, 4):
        group = ramp[start:start + 4]
        pattern.extend(group[2:] + group[:2])
    if {row[1] for row in data} != {nibble * 768 for nibble in range(16)}:
        raise SystemExit("Wave signal does not retain every programmed nibble level")
    step = 8 * (65536 // 300)
    period = step * 32
    quantum = math.gcd(data[1][0] - data[0][0], step)
    observations = [(row[0] % period, row[1]) for row in data]
    for phase in range(0, period, quantum):
        if all(pattern[((clock - phase) % period) // step] * 768 == value
               for clock, value in observations):
            return
    raise SystemExit("Wave signal differs from the complete source-defined pattern")


def noise(data):
    mono(data)
    if {row[1] for row in data} != {0, 9216}:
        raise SystemExit("Noise signal levels differ")
    # The programmed 15-bit polynomial advances every 128 clocks; captures
    # are 512 clocks apart. Compare its entire decimated sequence up to phase.
    state = 0
    bits = []
    for _ in range(32767):
        bit = (state ^ (state >> 1) ^ 1) & 1
        bits.append(bit)
        state = (state >> 1) | (bit << 14)
    sequence = "".join(str(bits[(i * 4) % 32767]) for i in range(32767))
    observed = "".join("1" if row[1] else "0" for row in data)
    extended = sequence * (2 + len(observed) // len(sequence))
    phase = extended.find(observed[:64], 0, 32767 + 64)
    if phase < 0 or extended[phase:phase + len(observed)] != observed:
        raise SystemExit("Noise signal differs from the programmed polynomial sequence")


def tone_sequence(data, frequencies, frames_per_tone):
    # Locate the run independently of drawing time and boot/announcement phase.
    # Every declared tone must fit, in order, at the original frame spacing.
    for begin in range(150, 281):
        try:
            for index, frequency in enumerate(frequencies):
                start = begin + index * frames_per_tone + 4
                selected = window(data, start, start + 8)
                mono(selected)
                square(selected, frequency)
            return
        except SystemExit:
            continue
    raise SystemExit("The complete declared tone sequence is missing or differs")


def qualify(case, data):
    if case == "tone":
        selected = window(data, 100, 200)
        mono(selected)
        square(selected, 1000)
    elif case == "stereo":
        selected = window(data, 190, 220)
        square(selected, 330, 1)
        square(selected, 1320, 2)
        selected = window(data, 350, 380)
        square(selected, 440, 1)
        if any(row[2] for row in selected):
            raise SystemExit("Left-only signal reaches the right output")
        selected = window(data, 440, 470)
        square(selected, 880, 2)
        if any(row[1] for row in selected):
            raise SystemExit("Right-only signal reaches the left output")
    elif case == "sweep":
        tone_sequence(data, (80, 110, 150, 200, 280, 380, 520, 700, 950, 1300,
                            1800, 2400, 3200, 4300, 5800, 7800, 10000, 13000, 16000), 16)
    elif case == "steps":
        tone_sequence(data, (100, 250, 500, 1000, 2000, 4000, 8000, 12000, 16000), 40)
    elif case == "wave":
        wave(window(data, 350, 470))
    elif case == "noise":
        noise(window(data, 340, 440))
    elif case == "dynamic":
        selected = window(data, 230, 620)
        mono(selected)
        changes = []
        for _, value, _ in selected:
            if value > 0 and (not changes or value != changes[-1]):
                changes.append(value)
        first = changes.index(15 * 768) if 15 * 768 in changes else -1
        if first < 0 or changes[first:first + 15] != [level * 768 for level in range(15, 0, -1)]:
            raise SystemExit("Dynamic signal does not complete the declared volume ramp")
    elif case == "bias":
        durations = [row[0] - (data[i - 1][0] if i else 0) for i, row in enumerate(data)]
        for interval in (512, 256, 128, 64):
            runs = []
            begin = None
            for i, duration in enumerate(durations + [0]):
                if duration == interval and begin is None:
                    begin = i
                elif duration != interval and begin is not None:
                    runs.append((begin, i))
                    begin = None
            if not runs:
                raise SystemExit(f"Missing {interval}-clock sampling mode")
            begin, end = max(runs, key=lambda run: run[1] - run[0])
            cutoff = data[end - 1][0] - 30 * FRAME_CLOCKS
            selected = [row for row in data[begin:end] if row[0] >= cutoff]
            if len(selected) < 30 * FRAME_CLOCKS // interval:
                raise SystemExit("Incomplete stable sampling-mode segment")
            mono(selected)
            square(selected, 1000)
    elif case == "fifo":
        selected = window(data, 390, 415)
        mono(selected)
        levels = {round(100 * math.sin(2 * math.pi * i / 256)) * 192 for i in range(256)}
        actual = {row[1] for row in selected}
        if actual != levels:
            raise SystemExit("FIFO signal differs from the signed source table levels")
        # Require the programmed hold period without assigning an unmeasured
        # absolute timer/mixer phase or claiming a hardware sine trace.
        edges = [b[0] for a, b in zip(selected, selected[1:]) if a[1] != b[1]]
        if not edges or len({edge % 1024 for edge in edges}) != 1:
            raise SystemExit("FIFO hold changes do not follow the programmed timer")
    elif case == "silence":
        if any(left or right for _, left, right in window(data, 430, 550)):
            raise SystemExit("The declared silence interval produces output")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--test", choices=CASES)
    args = parser.parse_args()
    if hashlib.sha256(args.rom.read_bytes()).hexdigest() != ROM_SHA256:
        raise SystemExit("Audio cartridge checksum mismatch")
    for case in (args.test,) if args.test else CASES:
        menu, frames = CASES[case]
        with tempfile.TemporaryDirectory(prefix="cupidgba-audio-") as directory:
            capture = Path(directory) / "audio.pcm"
            command = [str(args.runner.resolve()), str(args.rom.resolve()), "--frames", str(frames),
                       "--dump-audio", str(capture)]
            for press in range(menu):
                command.extend(["--key-event", str(100 + 12 * press), "256",
                                "--key-event", str(106 + 12 * press), "0"])
            result = subprocess.run(command, check=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, timeout=120)
            print(result.stdout, end="")
            raw = capture.read_bytes()
        if raw[:8] != b"CPDPCM01" or not (len(raw) - 8) % 12 == 0:
            raise SystemExit("Incomplete PCM observer capture")
        data = list(struct.iter_unpack("<Qhh", raw[8:]))
        stats = re.search(r"frames=(\d+) cycles=(\d+)", result.stdout)
        if not stats or int(stats[1]) != frames or not data or data[0][0] != 512:
            raise SystemExit("Incomplete audio cartridge execution")
        durations = [row[0] - (data[i - 1][0] if i else 0) for i, row in enumerate(data)]
        if any(duration not in range(64, 513, 64) for duration in durations):
            raise SystemExit("Missing or non-monotonic mixer captures")
        if case != "bias" and (any(duration != 512 for duration in durations) or
                               len(data) != int(stats[2]) // 512):
            raise SystemExit("Incomplete default-rate capture sequence")
        qualify(case, data)
        print(f"PASS audio {case}: {len(data)} complete capture records")


if __name__ == "__main__":
    main()
