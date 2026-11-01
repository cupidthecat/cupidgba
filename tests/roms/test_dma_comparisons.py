"""Check DMA input provenance, both live operands and complete result records."""

# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import contextlib
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import check_dma_probe as probe
from protocols.comparisons import observation_arguments, require_result, verify_input
from test_immediate_comparisons import comparison_image


def owned_image(original):
    case, data = comparison_image(original)
    data = bytearray(data)
    for observation in case["observations"]:
        if "second_register" not in observation:
            continue
        offset = int(observation["address"], 16) - 0x08000000
        literal = int(observation["literal"], 16) - 0x08000000
        second = observation["second_register"]
        shift = observation["literal_shift"]
        struct.pack_into("<I", data, literal, 0x84000001)
        struct.pack_into("<I", data, offset - 4, 0xE1A00020 | second << 12 | shift << 7 | second)
    case["sha256"] = hashlib.sha256(data).hexdigest()
    return case, data


def result_lines(case):
    lines = []
    for index, item in enumerate(case["observations"]):
        value = int(item["value"], 16)
        second = (
            f" r{item['second_register']}={value:08x} expected={value:08x}"
            if "second_register" in item
            else ""
        )
        lines.append(
            f"observation={index} pc={int(item['address'], 16):08x} mode={item['mode']} "
            f"opcode={int(item['instruction'], 16):08x} "
            f"r{item['register']}={value:08x} expected={value:08x}{second} result=PASS"
        )
    return lines + [
        f"observations={len(lines)}/{len(lines)} result=PASS",
        "checkpoint=yes r12=0 result=PASS",
        "terminal=yes r12=0 result=PASS",
        f"terminal-issued=yes completed=yes pc={int(case['terminal'], 16):08x}",
    ]


class DMAComparisons(unittest.TestCase):
    def setUp(self):
        self.cases = json.loads(Path(__file__).with_name("dma-probes.json").read_text())["cases"]

    def test_all_original_contracts(self):
        self.assertEqual(len(self.cases), 9)
        self.assertEqual(sum(len(case["observations"]) for case in self.cases), 10)
        for original in self.cases:
            case, data = owned_image(original)
            verify_input(data, case)
            require_result("\n".join(result_lines(case)), 0, case)
            arguments = observation_arguments(case)
            self.assertEqual(
                arguments.count("--expect-register-pair-at"), original["name"] == "mode-change"
            )
            self.assertEqual(arguments.count("--expect-register-at"), 1)

    def test_each_required_record(self):
        for case in self.cases:
            lines = result_lines(case)
            for index in range(len(lines)):
                with self.subTest(case=case["name"], line=index):
                    for changed in [lines[:index] + lines[index + 1 :], lines + [lines[index]]]:
                        with self.assertRaises(ValueError):
                            require_result("\n".join(changed), 0, case)
            with self.assertRaises(ValueError):
                require_result("\n".join(lines), 1, case)
            for extra in ["result=FAIL", "observation-order=FAIL", "terminal=no", "checkpoint=no"]:
                with self.assertRaises(ValueError):
                    require_result("\n".join(lines + [extra]), 0, case)

    def test_each_comparison_encoding(self):
        for original in self.cases:
            case, data = owned_image(original)
            for index, item in enumerate(case["observations"]):
                changes = {
                    "value": "0x0",
                    "register": item["register"] ^ 1,
                    "instruction": hex(int(item["instruction"], 16) ^ 1),
                    "mode": "thumb" if item["mode"] == "arm" else "arm",
                    "address": hex(int(item["address"], 16) + 1),
                }
                for key, value in changes.items():
                    changed = copy.deepcopy(case)
                    changed["observations"][index][key] = value
                    with self.subTest(case=case["name"], index=index, key=key):
                        with self.assertRaises(ValueError):
                            verify_input(data, changed)

    def test_checker_keeps_execution_guards(self):
        case, data = owned_image(self.cases[0])
        with tempfile.TemporaryDirectory(prefix="cupid-dma-contract-") as directory:
            path = Path(directory) / case["path"]
            path.write_bytes(data)
            runner = Path(directory) / "owned-runner"
            bios = Path(directory) / "owned-bios"
            argv = [
                "check_dma_probe.py",
                "--directory",
                directory,
                "--runner",
                str(runner),
                "--test",
                case["name"],
                "--bios",
                str(bios),
            ]
            result = subprocess.CompletedProcess([], 0, "\n".join(result_lines(case)), "")
            with patch.object(sys, "argv", argv), patch.object(
                probe.json, "loads", return_value={"cases": [case]}
            ), patch.object(
                probe.subprocess, "run", return_value=result
            ) as run, contextlib.redirect_stdout(
                io.StringIO()
            ):
                probe.main()
                command = run.call_args.args[0]
                self.assertEqual(command[:2], [str(runner.resolve()), str(path.resolve())])
                for option, value in [
                    ("--frames", "360"),
                    ("--steps", "150000000"),
                    ("--expect-loop-register", "12"),
                    ("--checkpoint-address", case["checkpoint"]),
                    ("--terminal-address", case["terminal"]),
                    ("--bios", str(bios.resolve())),
                ]:
                    self.assertEqual(command[command.index(option) + 1], value)
                self.assertEqual(run.call_args.kwargs["timeout"], 90)
                self.assertIn("--expect-register-pair-at", command)
                run.reset_mock()
                path.write_bytes(data[:-1])
                with self.assertRaises(SystemExit):
                    probe.main()
                run.assert_not_called()

    def test_live_register_pair(self):
        case = self.cases[0]
        lines = result_lines(case)
        for token in ["r0=00008400", "r2=00008400"]:
            changed = lines.copy()
            changed[1] = changed[1].replace(token, token[:3] + "00000000")
            with self.assertRaises(ValueError):
                require_result("\n".join(changed), 0, case)
        changed = lines.copy()
        changed[1] = changed[1].replace(" r2=00008400 expected=00008400", "")
        with self.assertRaises(ValueError):
            require_result("\n".join(changed), 0, case)
        with self.assertRaises(ValueError):
            require_result("\n".join([lines[1], lines[0], *lines[2:]]), 0, case)

    def test_literal_shift_and_operands(self):
        case, data = owned_image(self.cases[0])
        for key, values in {
            "literal": ["0x7fffffc", "0x8000189", case["checkpoint"]],
            "literal_shift": [0, 15, 17, 32, True],
            "second_register": [0, 1, 15, -1, True],
            "register": [1, 15, -1, True],
            "mode": ["thumb", "invalid"],
            "value": ["0x0", "0x8401", "0x100000000", "-0x1"],
            "instruction": ["0xe1500000", "0xe1500082", "0x11500002"],
        }.items():
            for value in values:
                changed = copy.deepcopy(case)
                changed["observations"][1][key] = value
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    verify_input(data, changed)
        for offset in [0x188, 0x18A, 0x1B8, 0x1BA, 0x1BC]:
            changed = bytearray(data)
            changed[offset] ^= 1
            adjusted = copy.deepcopy(case)
            adjusted["sha256"] = hashlib.sha256(changed).hexdigest()
            if offset == 0x188:
                # Low literal bits are discarded by the original shift.
                verify_input(changed, adjusted)
            else:
                with self.assertRaises(ValueError):
                    verify_input(changed, adjusted)

    def test_input_identity_and_empty_protocol(self):
        for original in self.cases:
            case, data = owned_image(original)
            with self.assertRaises(ValueError):
                verify_input(data[:-1], case)
            case["observations"] = []
            with self.assertRaises(ValueError):
                verify_input(data, case)
            with self.assertRaises(ValueError):
                require_result("\n".join(result_lines(case)), 0, case)


if __name__ == "__main__":
    unittest.main()
