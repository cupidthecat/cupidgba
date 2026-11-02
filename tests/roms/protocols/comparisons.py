# @file comparisons.py
# @brief Validate original comparison operands and completed cartridge result boundaries.
"""Validate original comparison operands and completed cartridge result boundaries."""

# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import struct

# This fixed ARM sequence waits for the next VBlank and restores r0/r1.
DISPLAY_ENTRY = struct.pack(
    "<9I",
    0xE92D0003,
    0xE3A00301,
    0xE5901004,
    0xE3110001,
    0x1AFFFFFC,
    0xE5901004,
    0xE3110001,
    0x0AFFFFFC,
    0xE8BD0003,
)


def result_register(case):
    register = case.get("result_register", 12)
    if type(register) is not int or not 0 <= register <= 12:
        raise ValueError("Unsupported result register")
    return register


def comparison_opcode(mode, register, value):
    if type(register) is not int or not 0 <= value <= 255:
        raise ValueError("Unsupported immediate comparison")
    if mode == "thumb" and 0 <= register <= 7:
        return 0x2800 | register << 8 | value, 2
    if mode == "arm" and 0 <= register < 15:
        # Only unconditional CMP with an unrotated immediate is supported.
        # PC operands need separate qualification of the observed PC value.
        return 0xE3500000 | register << 16 | value, 4
    raise ValueError("Unsupported immediate comparison")


def observation_opcode(observation):
    register = observation["register"]
    value = int(observation["value"], 16)
    if "second_register" not in observation:
        return comparison_opcode(observation["mode"], register, value)
    second = observation["second_register"]
    if (
        observation["mode"] != "arm"
        or type(register) is not int
        or type(second) is not int
        or not 0 <= register < 15
        or not 0 <= second < 15
        or not 0 <= value <= 0xFFFFFFFF
    ):
        raise ValueError("Unsupported register comparison")
    # Register operands have no shift or PC adjustment in this protocol.
    return 0xE1500000 | register << 16 | second, 4


def observation_arguments(case):
    arguments = []
    for observation in case["observations"]:
        pair = "second_register" in observation
        arguments += [
            "--expect-register-pair-at" if pair else "--expect-register-at",
            observation["address"],
            str(observation["register"]),
            observation["value"],
        ]
        if pair:
            arguments += [str(observation["second_register"]), observation["value"]]
    return arguments


def verify_input(data, case):
    if hashlib.sha256(data).hexdigest() != case["sha256"]:
        raise ValueError("Comparison probe checksum mismatch")
    checkpoint = int(case["checkpoint"], 16) - 0x08000000
    signature = DISPLAY_ENTRY
    if (
        checkpoint < 0
        or checkpoint % 4
        or data[checkpoint : checkpoint + len(signature)] != signature
    ):
        raise ValueError("Comparison checkpoint does not precede the original display routine")
    terminal = int(case["terminal"], 16) - 0x08000000
    if (
        terminal < 0
        or terminal % 4
        or data[terminal : terminal + 4] != struct.pack("<I", 0xEAFFFFFE)
    ):
        raise ValueError("Comparison terminal opcode mismatch")
    if data[terminal - 4 : terminal] != struct.pack("<I", 0xE8BD1FFF):
        raise ValueError("Original result-register restoration is missing")
    result = struct.pack("<2I", 0xE92D1FFF, 0xE1B0C000 | result_register(case))
    result_start = checkpoint + len(signature)
    if (
        result_start + len(result) > terminal
        or data[result_start : result_start + len(result)] != result
    ):
        raise ValueError("Original result register or evaluation entry mismatch")
    if not case["observations"]:
        raise ValueError("Comparison probe has no comparisons")
    previous = -1
    for observation in case["observations"]:
        offset = int(observation["address"], 16) - 0x08000000
        opcode = int(observation["instruction"], 16)
        value = int(observation["value"], 16)
        expected, width = observation_opcode(observation)
        if opcode != expected:
            raise ValueError("Comparison changes the original operands")
        if (
            offset <= previous
            or offset % width
            or offset >= checkpoint
            or data[offset : offset + width] != opcode.to_bytes(width, "little")
        ):
            raise ValueError("Comparison opcode or order mismatch")
        if "second_register" in observation:
            literal = int(observation["literal"], 16) - 0x08000000
            second = observation["second_register"]
            if "literal_load" in observation:
                setup = int(observation["literal_load"], 16) - 0x08000000
                displacement = literal - setup - 8
                # These probes form a ROM literal address with ADD Rd, PC,
                # #imm8, then load that same register without writeback.
                if (
                    "literal_shift" in observation
                    or setup < 0
                    or setup % 4
                    or setup + 8 > offset
                    or literal < 0
                    or literal % 4
                    or literal + 4 > checkpoint
                    or not 0 <= displacement <= 255
                    or data[setup : setup + 8]
                    != struct.pack(
                        "<2I",
                        0xE28F0000 | second << 12 | displacement,
                        0xE5900000 | second << 16 | second << 12,
                    )
                    or int.from_bytes(data[literal : literal + 4], "little") != value
                ):
                    raise ValueError("Register comparison changes its original literal load")
                previous = offset
                continue
            shift = observation["literal_shift"]
            if (
                type(shift) is not int
                or not 1 <= shift <= 31
                or literal < 0
                or literal % 4
                or literal + 4 > checkpoint
                or int.from_bytes(data[literal : literal + 4], "little") >> shift != value
                or offset < 4
                or data[offset - 4 : offset]
                != struct.pack("<I", 0xE1A00020 | second << 12 | shift << 7 | second)
            ):
                raise ValueError("Register comparison changes its literal or preceding shift")
        previous = offset


def require_result(output, returncode, case):
    if not case["observations"]:
        raise ValueError("Comparison result contract has no comparisons")
    lines = output.splitlines()
    expected = []
    for index, observation in enumerate(case["observations"]):
        value = int(observation["value"], 16)
        opcode, _ = observation_opcode(observation)
        if opcode != int(observation["instruction"], 16):
            raise ValueError("Comparison result contract changes the comparison opcode")
        second = (
            f" r{observation['second_register']}={value:08x} expected={value:08x}"
            if "second_register" in observation
            else ""
        )
        expected.append(
            f"observation={index} pc={int(observation['address'], 16):08x} mode={observation['mode']} "
            f"opcode={int(observation['instruction'], 16):08x} "
            f"r{observation['register']}={value:08x} expected={value:08x}{second} result=PASS"
        )
    count = len(expected)
    register = result_register(case)
    boundaries = [
        f"observations={count}/{count} result=PASS",
        f"checkpoint=yes r{register}=0 result=PASS",
        f"terminal=yes r{register}=0 result=PASS",
        f"terminal-issued=yes completed=yes pc={int(case['terminal'], 16):08x}",
    ]
    # Some failure paths copy the measured byte into the result register.
    # A wrong zero byte can therefore leave a passing-looking terminal.
    if (
        returncode
        or [line for line in lines if line.startswith("observation=")] != expected
        or any(lines.count(line) != 1 for line in boundaries)
        or any(
            sum(line.startswith(prefix) for line in lines) != 1
            for prefix in ("observations=", "checkpoint=", "terminal=", "terminal-issued=")
        )
        or any("result=FAIL" in line or "observation-order=FAIL" in line for line in lines)
    ):
        raise ValueError(
            "Comparison probe did not satisfy its comparisons and completed boundaries"
        )
