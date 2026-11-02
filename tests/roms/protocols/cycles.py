# @file cycles.py
# @brief Validate repeated display-control stores without treating them as pixel comparisons.
"""Validate repeated display-control stores without treating them as pixel comparisons."""

# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import re


def verify_input(data, case):
    if hashlib.sha256(data).hexdigest() != case["sha256"]:
        raise ValueError("Cyclic display input checksum mismatch")
    if (len(case["observations"]) != 2 or type(case["minimum_cycles"]) is not int
            or not 0 < case["minimum_cycles"] <= 0xFFFFFFFFFFFFFFFF):
        raise ValueError("Cyclic display contract requires repeated enable/disable stores")
    previous = -1
    values = []
    for observation in case["observations"]:
        register, base = observation["register"], observation["base_register"]
        if type(register) is not int or type(base) is not int or not (0 <= register < 8 and 0 <= base < 8):
            raise ValueError("Unsupported Thumb store registers")
        opcode = 0x8000 | base << 3 | register
        offset = int(observation["address"], 16) - 0x08000000
        value = int(observation["value"], 16)
        if (offset <= previous or offset % 2 or offset < 0
                or int(observation["instruction"], 16) != opcode
                or data[offset:offset + 2] != opcode.to_bytes(2, "little")
                or int(observation["base_value"], 16) != 0x04000000
                or not 0 <= value <= 0xFFFF):
            raise ValueError("Cyclic display store opcode or operand mismatch")
        previous = offset
        values.append(value)
    if not values[0] & 0x1000 or values[1] != values[0] & ~0x1000:
        raise ValueError("Cyclic display stores must change only OBJ enable")


def observation_arguments(case):
    arguments = ["--repeat-observations", str(case["minimum_cycles"])]
    for observation in case["observations"]:
        arguments += ["--expect-register-pair-at", observation["address"],
                      str(observation["register"]), observation["value"],
                      str(observation["base_register"]), observation["base_value"]]
    return arguments


def observation_line(index, observation):
    value, base = int(observation["value"], 16), int(observation["base_value"], 16)
    return (f"observation={index} pc={int(observation['address'], 16):08x} mode=thumb "
            f"opcode={int(observation['instruction'], 16):08x} "
            f"r{observation['register']}={value:08x} expected={value:08x} "
            f"r{observation['base_register']}={base:08x} expected={base:08x} result=PASS")


def require_result(stdout, returncode, case):
    lines = stdout.splitlines()
    expected = [observation_line(i, observation) for i, observation in enumerate(case["observations"])]
    records = [line for line in lines if line.startswith("observation=")]
    cycles, partial = divmod(len(records), len(expected))
    summary = (f"observation-cycles={cycles} minimum={case['minimum_cycles']} "
               f"partial={partial}/{len(expected)} result=PASS")
    frames = [line for line in lines if line.startswith("frames=")]
    progress = re.fullmatch(rf"frames={case['frames']} cycles=(\d+) steps=(\d+) pc=[0-9a-f]{{8}}",
                            frames[0]) if len(frames) == 1 else None
    if (returncode or cycles < case["minimum_cycles"]
            or any(line != expected[i % len(expected)] for i, line in enumerate(records))
            or [line for line in lines if line.startswith("observation-cycles=")] != [summary]
            or progress is None
            or int(progress[1]) <= 0 or not 0 < int(progress[2]) <= case["steps"]
            or any("result=FAIL" in line or "observation-order=FAIL" in line for line in lines)):
        raise ValueError("Cyclic display control stream is incomplete or inconsistent")
    return cycles, partial
