# @file test_bus_feedback_protocol.py
# @brief Reject incomplete or contradictory bus comparison results.
"""Reject incomplete or contradictory bus comparison results."""

# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import hashlib
import json
from pathlib import Path
import unittest
from check_bus_feedback import require_result, verify_input


class BusFeedbackProtocol(unittest.TestCase):
    def setUp(self):
        self.cases = json.loads(Path(__file__).with_name("bus-feedback-probes.json").read_text())[
            "cases"
        ]

    def lines(self, case):
        value = int(case["expected"], 16)
        return [
            f"observation=0 pc={int(case['address'], 16):08x} mode={case['mode']} "
            f"opcode={int(case['opcode'], 16):08x} r2={value:08x} expected={value:08x} result=PASS",
            "observations=1/1 result=PASS",
            "terminal=yes r12=0 result=PASS",
            f"terminal-issued=yes completed=yes pc={int(case['terminal'], 16):08x}",
        ]

    def test_complete(self):
        for case in self.cases:
            require_result("\n".join(self.lines(case)), 0, case)

    def test_missing_and_duplicated_boundaries(self):
        for case in self.cases:
            lines = self.lines(case)
            for index in range(len(lines)):
                with self.assertRaises(ValueError):
                    require_result("\n".join(lines[:index] + lines[index + 1 :]), 0, case)
                with self.assertRaises(ValueError):
                    require_result("\n".join(lines + [lines[index]]), 0, case)

    def test_wrong_observation_or_failure(self):
        for case in self.cases:
            lines = self.lines(case)
            for old, new in [
                ("mode=", "mode=wrong"),
                ("r2=", "r3="),
                ("completed=yes", "completed=no"),
                ("result=PASS", "result=FAIL"),
            ]:
                with self.assertRaises(ValueError):
                    require_result("\n".join(lines).replace(old, new), 0, case)
            for status, extra in [(1, ""), (0, "\nresult=FAIL"), (0, "\nobservation-order=FAIL")]:
                with self.assertRaises(ValueError):
                    require_result("\n".join(lines) + extra, status, case)

    def test_input_contract(self):
        for original in self.cases:
            case = copy.deepcopy(original)
            terminal = int(case["terminal"], 16) - 0x08000000
            address = int(case["address"], 16) - 0x08000000
            data = bytearray(terminal + 4)
            width = 4 if case["mode"] == "arm" else 2
            data[address : address + width] = int(case["opcode"], 16).to_bytes(width, "little")
            data[terminal : terminal + 4] = (0xEAFFFFFE).to_bytes(4, "little")
            case["sha256"] = hashlib.sha256(data).hexdigest()
            verify_input(data, case)
            for offset in [address, terminal]:
                changed = bytearray(data)
                changed[offset] ^= 1
                with self.assertRaises(ValueError):
                    verify_input(changed, case)
                altered = dict(case, sha256=hashlib.sha256(changed).hexdigest())
                with self.assertRaises(ValueError):
                    verify_input(changed, altered)
            for field, value in [("expected", "0x100"), ("mode", "invalid")]:
                with self.assertRaises(ValueError):
                    verify_input(data, dict(case, **{field: value}))


if __name__ == "__main__":
    unittest.main()
