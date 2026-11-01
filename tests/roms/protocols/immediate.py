"""Validate original immediate comparisons and completed cartridge result boundaries."""

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


def verify_input(data, case):
    if hashlib.sha256(data).hexdigest() != case["sha256"]:
        raise ValueError("Immediate probe checksum mismatch")
    checkpoint = int(case["checkpoint"], 16) - 0x08000000
    signature = DISPLAY_ENTRY
    if (
        checkpoint < 0
        or checkpoint % 4
        or data[checkpoint : checkpoint + len(signature)] != signature
    ):
        raise ValueError("Immediate checkpoint does not precede the original display routine")
    terminal = int(case["terminal"], 16) - 0x08000000
    if (
        terminal < 0
        or terminal % 4
        or data[terminal : terminal + 4] != struct.pack("<I", 0xEAFFFFFE)
    ):
        raise ValueError("Immediate terminal opcode mismatch")
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
        raise ValueError("Immediate probe has no comparisons")
    previous = -1
    for observation in case["observations"]:
        offset = int(observation["address"], 16) - 0x08000000
        opcode = int(observation["instruction"], 16)
        value = int(observation["value"], 16)
        register = observation["register"]
        expected, width = comparison_opcode(observation["mode"], register, value)
        if opcode != expected:
            raise ValueError("Immediate comparison changes the original register or immediate")
        if (
            offset <= previous
            or offset % width
            or offset >= checkpoint
            or data[offset : offset + width] != opcode.to_bytes(width, "little")
        ):
            raise ValueError("Immediate comparison opcode or order mismatch")
        previous = offset


def require_result(output, returncode, case):
    if not case["observations"]:
        raise ValueError("Immediate result contract has no comparisons")
    lines = output.splitlines()
    expected = []
    for index, observation in enumerate(case["observations"]):
        value = int(observation["value"], 16)
        opcode, _ = comparison_opcode(observation["mode"], observation["register"], value)
        if opcode != int(observation["instruction"], 16):
            raise ValueError("Immediate result contract changes the comparison opcode")
        expected.append(
            f"observation={index} pc={int(observation['address'], 16):08x} mode={observation['mode']} "
            f"opcode={int(observation['instruction'], 16):08x} "
            f"r{observation['register']}={value:08x} expected={value:08x} result=PASS"
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
        raise ValueError("Immediate probe did not satisfy its comparisons and completed boundaries")
