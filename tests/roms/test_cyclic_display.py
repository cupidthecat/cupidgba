# @file test_cyclic_display.py
# @brief Reject modified display stores, fabricated cycle counts and incomplete streams.
"""Reject modified display stores, fabricated cycle counts and incomplete streams."""

# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import hashlib
import json
from pathlib import Path
import unittest

from protocols.cycles import observation_arguments, require_result, verify_input


class CyclicDisplayProtocol(unittest.TestCase):
    def setUp(self):
        self.case = json.loads(Path(__file__).with_name("cyclic-display-probes.json").read_text())["cases"][0]
        self.data = bytearray(0x400)
        self.data[0x322:0x324] = bytes.fromhex("1a80")
        self.data[0x35e:0x360] = bytes.fromhex("1c80")
        self.case["sha256"] = hashlib.sha256(self.data).hexdigest()
        # Independently spell the original register pairs and instruction bytes.
        self.records = [
            "observation=0 pc=08000322 mode=thumb opcode=0000801a "
            "r2=00001404 expected=00001404 r3=04000000 expected=04000000 result=PASS",
            "observation=1 pc=0800035e mode=thumb opcode=0000801c "
            "r4=00000404 expected=00000404 r3=04000000 expected=04000000 result=PASS",
        ]

    def stream(self, partial=0, cycles=300):
        return "\n".join(self.records * cycles + self.records[:partial] + [
            "frames=600 cycles=168453824 steps=8362691 pc=00000170",
            f"observation-cycles={cycles} minimum=300 partial={partial}/2 result=PASS",
        ])

    def test_valid_bytes_streams_and_original_arguments(self):
        verify_input(self.data, self.case)
        for partial in (0, 1):
            for cycles in (300, 328, 598):
                self.assertEqual(require_result(self.stream(partial, cycles), 0, self.case), (cycles, partial))
        self.assertEqual(observation_arguments(self.case), [
            "--repeat-observations", "300",
            "--expect-register-pair-at", "0x08000322", "2", "0x1404", "3", "0x04000000",
            "--expect-register-pair-at", "0x0800035e", "4", "0x0404", "3", "0x04000000",
        ])

    def test_changed_input_and_rehashed_wrong_store(self):
        for offset in (0, 0x322, 0x35e):
            data = bytearray(self.data)
            data[offset] ^= 1
            with self.assertRaisesRegex(ValueError, "checksum"):
                verify_input(data, self.case)
            if offset:
                case = copy.deepcopy(self.case)
                case["sha256"] = hashlib.sha256(data).hexdigest()
                with self.assertRaisesRegex(ValueError, "opcode"):
                    verify_input(data, case)

    def test_changed_contract(self):
        for key, value in [("register", 8), ("base_register", 2), ("base_value", "0x04000002"),
                           ("address", "0x08000323"), ("instruction", "0x801b"),
                           ("value", "0x0404"), ("value", "0x10000")]:
            case = copy.deepcopy(self.case)
            case["observations"][0][key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                verify_input(self.data, case)
        for count in (0, -1, True, 1 << 64):
            case = copy.deepcopy(self.case)
            case["minimum_cycles"] = count
            with self.assertRaises(ValueError):
                verify_input(self.data, case)

    def test_missing_late_bad_reordered_and_fabricated_results(self):
        valid = self.stream(1, 328)
        variants = [
            self.stream(cycles=299),
            valid.replace(self.records[0], self.records[0].replace("mode=thumb", "mode=arm"), 1),
            valid.replace("opcode=0000801a", "opcode=0000801b", 1),
            valid.replace("r3=04000000 expected", "r3=04000002 expected", 1),
            valid.rsplit(self.records[0], 1)[0] + "observation-cycles=328 minimum=300 partial=1/2 result=PASS",
            valid.replace("observation-cycles=328", "observation-cycles=329"),
            valid.replace("partial=1/2", "partial=0/2"),
            valid.replace("minimum=300", "minimum=1"),
            valid.replace("frames=600", "frames=599"),
            valid.replace("steps=8362691", "steps=150000001"),
            valid + "\n" + self.records[1],
            valid + "\nobservation-order=FAIL pc=08000322 next=1",
            valid + "\nobservation-cycles=328 minimum=300 partial=1/2 result=PASS",
            valid + "\nframes=600 cycles=1 steps=1 pc=00000000",
            valid.replace(self.records[0] + "\n" + self.records[1], self.records[1] + "\n" + self.records[0], 1),
        ]
        # Corrupt the final issued store, after many complete valid cycles.
        before, after = valid.rsplit(self.records[0], 1)
        variants.append(before + self.records[0].replace("r2=00001404 expected", "r2=00000404 expected") + after)
        for index, stdout in enumerate(variants):
            with self.subTest(index=index), self.assertRaises(ValueError):
                require_result(stdout, 0, self.case)
        with self.assertRaises(ValueError):
            require_result(valid, 1, self.case)


if __name__ == "__main__":
    unittest.main()
