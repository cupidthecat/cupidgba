# @file test_register_comparisons.py
# @brief Reject altered literal loads, missing comparisons and false result boundaries.
"""Reject altered literal loads, missing comparisons and false result boundaries."""
# SPDX-License-Identifier: GPL-3.0-or-later
import contextlib
import copy
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

import check_register_probe as probe
from protocols.comparisons import require_result, verify_input
from test_immediate_comparisons import comparison_image


def owned_image(original):
    case, data = comparison_image(original)
    for item in case["observations"]:
        if "constant_setup" in item:
            setup = int(item["constant_setup"], 16) - 0x08000000
            second = item["second_register"]
            value = int(item["value"], 16)
            for index in range(4):
                prefix = 0xE3A00000 if index == 0 else 0xE3800000 | second << 16
                rotate = (16 - index * 4) % 16
                opcode = prefix | second << 12 | rotate << 8 | (value >> (index * 8) & 255)
                struct.pack_into("<I", data, setup + index * 4, opcode)
            continue
        if "literal_load" not in item:
            continue
        setup = int(item["literal_load"], 16) - 0x08000000
        literal = int(item["literal"], 16) - 0x08000000
        second = item["second_register"]
        struct.pack_into("<I", data, literal, int(item["value"], 16))
        struct.pack_into(
            "<2I", data, setup,
            0xE28F0000 | second << 12 | (literal - setup - 8),
            0xE5900000 | second << 16 | second << 12,
        )
    case["sha256"] = hashlib.sha256(data).hexdigest()
    return case, data


def result_lines(case):
    lines = []
    for index, item in enumerate(case["observations"]):
        value = int(item["value"], 16)
        pair = (
            f" r{item['second_register']}={value:08x} expected={value:08x}"
            if "second_register" in item else ""
        )
        lines.append(
            f"observation={index} pc={int(item['address'], 16):08x} mode={item['mode']} "
            f"opcode={int(item['instruction'], 16):08x} "
            f"r{item['register']}={value:08x} expected={value:08x}{pair} result=PASS"
        )
    register = case["result_register"]
    return lines + [
        f"observations={len(lines)}/{len(lines)} result=PASS",
        f"checkpoint=yes r{register}=0 result=PASS",
        f"terminal=yes r{register}=0 result=PASS",
        f"terminal-issued=yes completed=yes pc={int(case['terminal'], 16):08x}",
    ]


class RegisterComparisons(unittest.TestCase):
    def setUp(self):
        self.manifest = json.loads(Path(__file__).with_name("register-probes.json").read_text())
        self.cases = self.manifest["cases"]

    def test_original_contracts(self):
        self.assertEqual(len(self.cases), 11)
        self.assertEqual(sum(len(c["observations"]) for c in self.cases), 40)
        for original in self.cases:
            case, data = owned_image(original)
            verify_input(data, case)
            require_result("\n".join(result_lines(case)), 0, case)

    def test_each_result_record_is_required_once(self):
        for case in self.cases:
            lines = result_lines(case)
            for index in range(len(lines)):
                for altered in (lines[:index] + lines[index + 1:], lines + [lines[index]]):
                    with self.subTest(case=case["name"], index=index), self.assertRaises(ValueError):
                        require_result("\n".join(altered), 0, case)

    def test_both_live_operands_and_process_success(self):
        for case in self.cases:
            lines = result_lines(case)
            with self.assertRaises(ValueError):
                require_result("\n".join(lines), 1, case)
            for item in case["observations"]:
                value = int(item["value"], 16)
                for register in [item["register"]] + (
                    [item["second_register"]] if "second_register" in item else []
                ):
                    altered = "\n".join(lines).replace(
                        f"r{register}={value:08x} expected=", f"r{register}={value ^ 1:08x} expected=", 1
                    )
                    with self.assertRaises(ValueError):
                        require_result(altered, 0, case)

    def test_literal_address_load_value_and_order(self):
        original = next(c for c in self.cases if c["name"] == "palette-byte-writes")
        case, data = owned_image(original)
        first = case["observations"][0]
        setup = int(first["literal_load"], 16) - 0x08000000
        literal = int(first["literal"], 16) - 0x08000000
        for offset in [setup, setup + 4, literal]:
            changed = bytearray(data)
            changed[offset] ^= 1
            altered = copy.deepcopy(case)
            altered["sha256"] = hashlib.sha256(changed).hexdigest()
            with self.assertRaises(ValueError):
                verify_input(changed, altered)
        for name, value in [("literal_load", "0x7fffffc"), ("literal_load", "0x8000122"),
                            ("literal", "0x8000000"), ("literal_shift", 1)]:
            altered = copy.deepcopy(case)
            altered["observations"][0][name] = value
            with self.assertRaises(ValueError):
                verify_input(data, altered)
        for order in [[0, 0, 2, 3], [1, 0, 2, 3]]:
            altered = copy.deepcopy(case)
            altered["observations"] = [case["observations"][i] for i in order]
            with self.assertRaises(ValueError):
                verify_input(data, altered)

    def test_checker_retains_budgets_and_selected_result_register(self):
        for original in self.cases:
            case, data = owned_image(original)
            with tempfile.TemporaryDirectory(prefix="cupid-register-check-") as temporary:
                directory = Path(temporary)
                path = directory / case["path"]
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
                runner, bios = directory / "owned-runner", directory / "owned-bios"
                argv = ["check_register_probe.py", "--directory", temporary, "--runner", str(runner),
                        "--test", case["name"], "--bios", str(bios)]
                result = subprocess.CompletedProcess([], 0, "\n".join(result_lines(case)), "")
                with patch.object(sys, "argv", argv), patch.object(
                    probe.json, "loads", return_value={"cases": [case]}
                ), patch.object(probe.subprocess, "run", return_value=result) as run, \
                        contextlib.redirect_stdout(io.StringIO()):
                    probe.main()
                command = run.call_args.args[0]
                for option, value in [("--frames", "360"), ("--steps", "150000000"),
                                      ("--expect-loop-register", str(case["result_register"])),
                                      ("--bios", str(bios.resolve()))]:
                    self.assertEqual(command[command.index(option) + 1], value)
                self.assertEqual(run.call_args.kwargs["timeout"], 90)

    def test_constant_bytes_registers_flags_and_instruction_order(self):
        original = next(c for c in self.cases if c["name"] == "bios-protected-input")
        case, data = owned_image(original)
        for item in case["observations"]:
            setup = int(item["constant_setup"], 16) - 0x08000000
            for index in range(4):
                for mask in [1, 1 << 12, 1 << 20, 1 << 28]:
                    altered = bytearray(data)
                    offset = setup + index * 4
                    opcode = int.from_bytes(altered[offset:offset + 4], "little") ^ mask
                    struct.pack_into("<I", altered, offset, opcode)
                    changed = copy.deepcopy(case)
                    changed["sha256"] = hashlib.sha256(altered).hexdigest()
                    with self.assertRaises(ValueError):
                        verify_input(altered, changed)
        for key, value in [("constant_setup", "0x7fffffc"), ("constant_setup", "0x80000fe"),
                           ("constant_setup", "0x8000110"), ("literal", "0x8000000"),
                           ("literal_shift", 1), ("value", "0x0")]:
            changed = copy.deepcopy(case)
            changed["observations"][0][key] = value
            with self.assertRaises(ValueError):
                verify_input(data, changed)
        changed = copy.deepcopy(case)
        changed["observations"] = [case["observations"][0]] * 2
        with self.assertRaises(ValueError):
            verify_input(data, changed)

    def test_missing_firmware_precondition_never_launches_core(self):
        argv = ["check_register_probe.py", "--directory", "unused", "--runner", "unused",
                "--test", "timer-boot-retention"]
        with patch.object(sys, "argv", argv), patch.object(probe.subprocess, "run") as run, \
                contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            probe.main()
        run.assert_not_called()

    def test_fetch_retains_license_inputs_and_modified_cache(self):
        case, data = owned_image(self.cases[0])
        notice = b"Owned permission notice"
        manifest = dict(self.manifest, license_sha256=hashlib.sha256(notice).hexdigest())
        with tempfile.TemporaryDirectory(prefix="cupid-register-fetch-") as temporary, patch.object(
            probe, "download_verified", side_effect=[notice, data]
        ) as fetch:
            directory = Path(temporary)
            probe.fetch_cases(directory, manifest, [case])
            self.assertEqual(fetch.call_count, 2)
            path = directory / case["path"]
            self.assertEqual(path.read_bytes(), data)
            self.assertEqual((directory / "register-probe-LICENSE.txt").read_bytes(), notice)
            before = path.stat().st_mtime_ns
            probe.fetch_cases(directory, manifest, [case])
            self.assertEqual(fetch.call_count, 2)
            self.assertEqual(path.stat().st_mtime_ns, before)
            path.write_bytes(b"Changed private input")
            with self.assertRaises(ValueError):
                probe.fetch_cases(directory, manifest, [case])
            self.assertEqual(path.read_bytes(), b"Changed private input")
            self.assertEqual(fetch.call_count, 2)

    def test_failed_fetch_and_outside_path_publish_nothing(self):
        case, data = owned_image(self.cases[0])
        notice = b"Owned permission notice"
        manifest = dict(self.manifest, license_sha256=hashlib.sha256(notice).hexdigest())
        with tempfile.TemporaryDirectory(prefix="cupid-register-fetch-failure-") as temporary:
            directory = Path(temporary) / "inputs"
            for result in [OSError("transport failure"), b"Invalid cartridge layout"]:
                with patch.object(probe, "download_verified", side_effect=[notice, result]), \
                        self.assertRaises((OSError, ValueError)):
                    probe.fetch_cases(directory, manifest, [case])
                self.assertFalse(directory.exists())
            changed = dict(case, path="../outside.gba")
            with patch.object(probe, "download_verified") as fetch, self.assertRaises(ValueError):
                probe.fetch_cases(directory, manifest, [changed])
            fetch.assert_not_called()


if __name__ == "__main__":
    unittest.main()
