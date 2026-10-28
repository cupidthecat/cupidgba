"""Keep partial or contradictory cartridge observations out of the result gate."""
# SPDX-License-Identifier: GPL-3.0-or-later
import json
import hashlib
import copy
from pathlib import Path
import unittest
from check_thumb_exchange import require_result, verify_input


class ResultProtocol(unittest.TestCase):
    def setUp(self):
        self.case = json.loads(Path(__file__).with_name("thumb-exchange-probes.json").read_text())["cases"][0]
        self.lines = [
            f"observation={i} pc={int(row['address'], 16):08x} mode={row['mode']} "
            f"opcode={int(row['instruction'], 16):08x} r{row['register']}={row['value']:08x} "
            f"expected={row['value']:08x} result=PASS"
            for i, row in enumerate(self.case["observations"])
        ] + [f'terminal-issued=yes completed=yes pc={int(self.case["terminal"],16):08x}',
             "terminal=yes r12=0 result=PASS", "checkpoint=yes r12=0 result=PASS", f'observations={len(self.case["observations"])}/{len(self.case["observations"])} result=PASS']

    def test_complete(self):
        require_result("\n".join(self.lines), 0, self.case)

    def test_missing_each_boundary(self):
        for i in range(len(self.lines)):
            with self.subTest(boundary=i), self.assertRaises(ValueError):
                require_result("\n".join(self.lines[:i] + self.lines[i + 1:]), 0, self.case)

    def test_changed_or_reordered_observation(self):
        for replacement in [self.lines[0].replace("00004780", "00004700"),
                            self.lines[0].replace("mode=thumb", "mode=arm"),
                            self.lines[0].replace("r1=00000008", "r1=00000004")]:
            with self.assertRaises(ValueError):
                require_result("\n".join([replacement] + self.lines[1:]), 0, self.case)
        with self.assertRaises(ValueError):
            require_result("\n".join([self.lines[1], self.lines[0]] + self.lines[2:]), 0, self.case)

    def test_failure_status_and_extra_failure(self):
        for status, extra in [(1, ""), (0, "\nobservation-order=FAIL"), (0, "\nresult=FAIL")]:
            with self.assertRaises(ValueError):
                require_result("\n".join(self.lines) + extra, status, self.case)

    def test_duplicate_terminal(self):
        for line in self.lines[len(self.case["observations"]):]:
            with self.assertRaises(ValueError):
                require_result("\n".join(self.lines + [line]), 0, self.case)

    def test_incomplete_terminal(self):
        for before, after in [("terminal-issued=yes", "terminal-issued=no"),
                              ("completed=yes", "completed=no")]:
            lines = [line.replace(before, after) for line in self.lines]
            with self.assertRaises(ValueError):
                require_result("\n".join(lines), 0, self.case)

    def input_fixture(self):
        # This owned byte layout tests input validation, not CPU execution.
        data = bytearray(self.case["bytes"])
        data[0x140:0x144] = bytes.fromhex("082103a0")
        for row in self.case["observations"]:
            width = 2 if row["mode"] == "thumb" else 4
            offset = int(row["address"], 16) - 0x08000000
            data[offset:offset + width] = int(row["instruction"], 16).to_bytes(width, "little")
        data[0x174:0x184] = bytes.fromhex("03002de90103a0e3041090e5010011e3")
        data[0x2EC:0x2F0] = bytes.fromhex("feffffea")
        case = copy.deepcopy(self.case)
        case["sha256"] = hashlib.sha256(data).hexdigest()
        return data, case

    def test_input_digest(self):
        data, case = self.input_fixture()
        verify_input(data, case)
        data[-1] ^= 1
        with self.assertRaises(ValueError):
            verify_input(data, case)

    def test_input_layout_independently_of_digest(self):
        for offset in (0x140, 0x144, 0x150, 0x160, 0x174, 0x2EC):
            with self.subTest(offset=offset):
                data, case = self.input_fixture()
                data[offset] ^= 1
                case["sha256"] = hashlib.sha256(data).hexdigest()
                with self.assertRaises(ValueError):
                    verify_input(data, case)


if __name__ == "__main__":
    unittest.main()
