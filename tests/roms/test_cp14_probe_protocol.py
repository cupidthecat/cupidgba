"""Reject altered CP14 expectations and incomplete timing/value/flag observations."""

# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import hashlib
import json
from pathlib import Path
import struct
import unittest

from check_cp14_probe import expected_lines, require_result, verify_input


class Cp14ProbeProtocol(unittest.TestCase):
    def setUp(self):
        self.case = json.loads(Path(__file__).with_name("cp14-probe.json").read_text())
        self.data = bytearray(0x400)

        def put(address, value):
            struct.pack_into("<I", self.data, int(address, 16) - 0x08000000, value)

        for observation in self.case["observations"]:
            put(observation["address"], int(observation["instruction"], 16))
            if "literal" in observation:
                put(observation["literal"], int(observation["value"], 16))
            if "sample_address" in observation:
                put(observation["sample_address"], int(observation["value"], 16))
        for i, value in enumerate([0xE92D0003, 0xE3A00301, 0xE5901004, 0xE3110001]):
            put(hex(int(self.case["checkpoint"], 16) + i * 4), value)
        put(self.case["terminal"], 0xEAFFFFFE)
        self.case["sha256"] = hashlib.sha256(self.data).hexdigest()
        self.lines = expected_lines(self.case) + [
            "observations=4/4 result=PASS",
            "checkpoint=yes r12=0 result=PASS",
            "terminal=yes r12=0 result=PASS",
            "terminal-issued=yes completed=yes pc=0800037c",
        ]

    def test_complete_input_and_result(self):
        verify_input(self.data, self.case)
        require_result("\n".join(self.lines), 0, self.case)

    def test_missing_duplicate_and_reordered_output(self):
        for index in range(len(self.lines)):
            for lines in [
                self.lines[:index] + self.lines[index + 1 :],
                self.lines + [self.lines[index]],
            ]:
                with self.assertRaises(ValueError):
                    require_result("\n".join(lines), 0, self.case)
        with self.assertRaises(ValueError):
            require_result("\n".join([self.lines[1], self.lines[0], *self.lines[2:]]), 0, self.case)

    def test_wrong_measurement_or_status_cannot_pass_zero_boundaries(self):
        for index, old, new in [
            (0, "r3=00000075", "r3=00000073"),
            (1, "r1=e20330ff", "r1=00000000"),
            (2, "r2=1a000008", "r2=00000000"),
            (3, "masked-cpsr=80000000", "masked-cpsr=00000000"),
        ]:
            lines = self.lines.copy()
            lines[index] = lines[index].replace(old, new)
            with self.assertRaises(ValueError):
                require_result("\n".join(lines), 0, self.case)
        for code, suffix in [(1, ""), (0, "\nresult=FAIL"), (0, "\nobservation-order=FAIL")]:
            with self.assertRaises(ValueError):
                require_result("\n".join(self.lines) + suffix, code, self.case)

    def test_changed_input_with_or_without_replacement_digest(self):
        addresses = [self.case["checkpoint"], self.case["terminal"]]
        for observation in self.case["observations"]:
            addresses += [
                observation[field]
                for field in ["address", "literal", "sample_address"]
                if field in observation
            ]
        for address in addresses:
            data = bytearray(self.data)
            # The status sample is qualified for its N bit, not unobserved bits.
            offset = int(address, 16) - 0x08000000
            data[offset + (3 if address == "0x080001c8" else 0)] ^= 0x80
            with self.assertRaises(ValueError):
                verify_input(data, self.case)
            altered = dict(self.case, sha256=hashlib.sha256(data).hexdigest())
            with self.assertRaises(ValueError):
                verify_input(data, altered)

    def test_changed_manifest_operands_and_status(self):
        for index, field, value in [
            (0, "value", "0x73"),
            (0, "register", 2),
            (1, "second_register", 2),
            (1, "literal", "0x08000001"),
            (2, "value", "0x1a000007"),
            (3, "mask", "0x40000000"),
            (3, "value", "0x0"),
            (3, "instruction", "0xea000008"),
        ]:
            case = copy.deepcopy(self.case)
            case["observations"][index][field] = value
            with self.assertRaises(ValueError):
                verify_input(self.data, case)
        with self.assertRaises(ValueError):
            verify_input(self.data, dict(self.case, observations=self.case["observations"][:-1]))


if __name__ == "__main__":
    unittest.main()
