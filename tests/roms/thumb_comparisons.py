"""Validate original Thumb comparisons and completed cartridge result boundaries."""

# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import struct


def verify_input(data, case):
    if hashlib.sha256(data).hexdigest() != case["sha256"]:
        raise ValueError("Thumb probe checksum mismatch")
    checkpoint = int(case["checkpoint"], 16) - 0x08000000
    signature = struct.pack("<4I", 0xE92D0003, 0xE3A00301, 0xE5901004, 0xE3110001)
    if checkpoint < 0 or data[checkpoint : checkpoint + len(signature)] != signature:
        raise ValueError("Thumb checkpoint does not precede the original display routine")
    terminal = int(case["terminal"], 16) - 0x08000000
    if terminal < 0 or data[terminal : terminal + 4] != struct.pack("<I", 0xEAFFFFFE):
        raise ValueError("Thumb terminal opcode mismatch")
    if not case["observations"]:
        raise ValueError("Thumb probe has no comparisons")
    previous = -1
    for observation in case["observations"]:
        offset = int(observation["address"], 16) - 0x08000000
        opcode = int(observation["instruction"], 16)
        value = int(observation["value"], 16)
        register = observation["register"]
        if observation["mode"] != "thumb" or not 0 <= register <= 7 or not 0 <= value <= 255:
            raise ValueError("Unsupported Thumb comparison")
        if opcode != (0x2800 | register << 8 | value):
            raise ValueError("Thumb comparison changes the original register or immediate")
        if (
            offset <= previous
            or offset & 1
            or offset >= checkpoint
            or data[offset : offset + 2] != struct.pack("<H", opcode)
        ):
            raise ValueError("Thumb comparison opcode or order mismatch")
        previous = offset


def require_result(output, returncode, case):
    lines = output.splitlines()
    expected = []
    for index, observation in enumerate(case["observations"]):
        value = int(observation["value"], 16)
        expected.append(
            f"observation={index} pc={int(observation['address'], 16):08x} mode=thumb "
            f"opcode={int(observation['instruction'], 16):08x} "
            f"r{observation['register']}={value:08x} expected={value:08x} result=PASS"
        )
    count = len(expected)
    boundaries = [
        f"observations={count}/{count} result=PASS",
        "checkpoint=yes r12=0 result=PASS",
        "terminal=yes r12=0 result=PASS",
        f"terminal-issued=yes completed=yes pc={int(case['terminal'], 16):08x}",
    ]
    # Some failure paths copy the measured byte into the result register.
    # A wrong zero byte can therefore leave a passing-looking terminal.
    if (
        returncode
        or [line for line in lines if line.startswith("observation=")] != expected
        or any(lines.count(line) != 1 for line in boundaries)
        or any("result=FAIL" in line or "observation-order=FAIL" in line for line in lines)
    ):
        raise ValueError("Thumb probe did not satisfy its comparisons and completed boundaries")
