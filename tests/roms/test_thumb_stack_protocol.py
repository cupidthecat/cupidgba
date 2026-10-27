"""Keep partial or contradictory cartridge observations out of the result gate."""
# SPDX-License-Identifier: GPL-3.0-or-later
import json
import hashlib
import copy
from pathlib import Path
import unittest
from check_thumb_stack import require_result, verify_input


class ResultProtocol(unittest.TestCase):
    def setUp(self):
        self.case = json.loads(Path(__file__).with_name("thumb-stack-probes.json").read_text())["cases"][0]
        self.lines = [
            f"observation={i} pc={int(row['address'], 16):08x} mode={row['mode']} "
            f"opcode={int(row['instruction'], 16):08x} r{row['register']}={row['value']:08x} "
            f"expected={row['value']:08x} result=PASS"
            for i, row in enumerate(self.case["observations"])
        ] + ["terminal=yes r12=0 result=PASS", "checkpoint=yes r12=0 result=PASS", "observations=3/3 result=PASS"]

    def test_complete(self):
        require_result("\n".join(self.lines), 0, self.case)

    def test_missing_each_boundary(self):
        for i in range(len(self.lines)):
            with self.subTest(boundary=i), self.assertRaises(ValueError):
                require_result("\n".join(self.lines[:i] + self.lines[i + 1:]), 0, self.case)

    def test_changed_or_reordered_observation(self):
        for replacement in [self.lines[0].replace("0000bc00", "0000bd00"),
                            self.lines[0].replace("mode=thumb", "mode=arm"),
                            self.lines[0].replace("r1=08000168", "r1=0800016c")]:
            with self.assertRaises(ValueError):
                require_result("\n".join([replacement] + self.lines[1:]), 0, self.case)
        with self.assertRaises(ValueError):
            require_result("\n".join([self.lines[1], self.lines[0]] + self.lines[2:]), 0, self.case)

    def test_failure_status_and_extra_failure(self):
        for status, extra in [(1, ""), (0, "\nobservation-order=FAIL"), (0, "\nresult=FAIL")]:
            with self.assertRaises(ValueError):
                require_result("\n".join(self.lines) + extra, status, self.case)

    def test_duplicate_terminal(self):
        with self.assertRaises(ValueError):
            require_result("\n".join(self.lines + [self.lines[3]]), 0, self.case)

    def input_fixture(self):
        # This owned byte layout tests input validation, not CPU execution.
        data = bytearray(self.case["bytes"])
        data[0x150:0x166] = bytes.fromhex("1440000000008000680100082068186002b4001c00bc")
        data[0x176:0x182] = bytes.fromhex("162805d1684638403c2804d1")
        for row in self.case["observations"]:
            width = 2 if row["mode"] == "thumb" else 4
            offset = int(row["address"], 16) - 0x08000000
            data[offset:offset + width] = int(row["instruction"], 16).to_bytes(width, "little")
        data[0x1A0:0x1B0] = bytes.fromhex("03002de90103a0e3041090e5010011e3")
        data[0x318:0x31C] = bytes.fromhex("feffffea")
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
        for offset in (0x150, 0x154, 0x158, 0x160, 0x164, 0x176, 0x17E, 0x1A0, 0x318):
            with self.subTest(offset=offset):
                data, case = self.input_fixture()
                data[offset] ^= 1
                case["sha256"] = hashlib.sha256(data).hexdigest()
                with self.assertRaises(ValueError):
                    verify_input(data, case)


if __name__ == "__main__":
    unittest.main()
