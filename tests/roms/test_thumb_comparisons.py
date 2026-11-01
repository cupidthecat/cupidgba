"""Reject changed Thumb comparisons, zero-result shortcuts and invalid serial inputs."""

# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import hashlib
import json
from pathlib import Path
import struct
import unittest
import tempfile
from unittest.mock import patch, call

import check_serial_probe as probe
import check_object_tail_probe as object_probe

from thumb_comparisons import require_result, verify_input


class ThumbComparisonProtocol(unittest.TestCase):
    def setUp(self):
        self.cases = json.loads(Path(__file__).with_name("serial-probes.json").read_text())["cases"]
        self.cases.append(
            json.loads(Path(__file__).with_name("object-tail-probe.json").read_text())["case"]
        )

    def lines(self, case):
        lines = []
        for index, observation in enumerate(case["observations"]):
            value = int(observation["value"], 16)
            lines.append(
                f"observation={index} pc={int(observation['address'], 16):08x} "
                f"mode=thumb opcode={int(observation['instruction'], 16):08x} "
                f"r{observation['register']}={value:08x} expected={value:08x} result=PASS"
            )
        count = len(lines)
        return lines + [
            f"observations={count}/{count} result=PASS",
            "checkpoint=yes r12=0 result=PASS",
            "terminal=yes r12=0 result=PASS",
            f"terminal-issued=yes completed=yes pc={int(case['terminal'], 16):08x}",
        ]

    def image(self, original):
        case = copy.deepcopy(original)
        terminal = int(case["terminal"], 16) - 0x08000000
        checkpoint = int(case["checkpoint"], 16) - 0x08000000
        data = bytearray(terminal + 4)
        data[checkpoint : checkpoint + 16] = struct.pack(
            "<4I", 0xE92D0003, 0xE3A00301, 0xE5901004, 0xE3110001
        )
        data[terminal : terminal + 4] = struct.pack("<I", 0xEAFFFFFE)
        for observation in case["observations"]:
            offset = int(observation["address"], 16) - 0x08000000
            data[offset : offset + 2] = struct.pack("<H", int(observation["instruction"], 16))
        case["sha256"] = hashlib.sha256(data).hexdigest()
        return case, data

    def test_all_original_comparisons(self):
        self.assertEqual(len(self.cases), 14)
        self.assertEqual(sum(len(case["observations"]) for case in self.cases), 25)
        for case in self.cases:
            require_result("\n".join(self.lines(case)), 0, case)
            synthetic_case, data = self.image(case)
            verify_input(data, synthetic_case)

    def test_missing_duplicate_and_reordered_observations(self):
        for case in self.cases:
            lines = self.lines(case)
            for index in range(len(lines)):
                with self.subTest(case=case["name"], line=index):
                    with self.assertRaises(ValueError):
                        require_result("\n".join(lines[:index] + lines[index + 1 :]), 0, case)
                    with self.assertRaises(ValueError):
                        require_result("\n".join(lines + [lines[index]]), 0, case)
            if len(case["observations"]) > 1:
                with self.assertRaises(ValueError):
                    require_result("\n".join([lines[1], lines[0], *lines[2:]]), 0, case)

    def test_wrong_zero_measurement_cannot_pass_zero_result(self):
        for case in self.cases:
            lines = self.lines(case)
            observation = case["observations"][0]
            register = observation["register"]
            value = int(observation["value"], 16)
            wrong = 0 if value else 1
            lines[0] = lines[0].replace(f"r{register}={value:08x}", f"r{register}={wrong:08x}")
            # Preserve passing-looking boundaries and status; require the actual operand.
            with self.assertRaises(ValueError):
                require_result("\n".join(lines), 0, case)

    def test_wrong_opcode_mode_or_incomplete_terminal(self):
        for case in self.cases:
            text = "\n".join(self.lines(case))
            for old, new in [
                ("mode=thumb", "mode=arm"),
                ("opcode=", "opcode=ff"),
                ("completed=yes", "completed=no"),
                ("result=PASS", "result=FAIL"),
            ]:
                with self.assertRaises(ValueError):
                    require_result(text.replace(old, new), 0, case)
            for code, extra in [(1, ""), (0, "\nresult=FAIL"), (0, "\nobservation-order=FAIL")]:
                with self.assertRaises(ValueError):
                    require_result(text + extra, code, case)

    def test_changed_input_even_with_updated_digest(self):
        for original in self.cases:
            case, data = self.image(original)
            for address in [
                case["checkpoint"],
                case["terminal"],
                *[row["address"] for row in case["observations"]],
            ]:
                altered = bytearray(data)
                altered[int(address, 16) - 0x08000000] ^= 1
                with self.assertRaises(ValueError):
                    verify_input(altered, case)
                with self.assertRaises(ValueError):
                    verify_input(altered, dict(case, sha256=hashlib.sha256(altered).hexdigest()))

    def test_changed_comparison_contract(self):
        for original in self.cases:
            case, data = self.image(original)
            for field, value in [
                ("register", 8),
                ("value", "0x100"),
                ("mode", "arm"),
                ("address", "0x7fffffe"),
                ("instruction", "0x2800"),
            ]:
                changed = copy.deepcopy(case)
                changed["observations"][0][field] = value
                with self.assertRaises(ValueError):
                    verify_input(data, changed)
            for observations in [[], case["observations"] * 2]:
                with self.assertRaises(ValueError):
                    verify_input(data, dict(case, observations=observations))


class SerialProbeInputs(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name) / "inputs"
        self.manifest = json.loads(Path(probe.__file__).with_name("serial-probes.json").read_text())
        self.base = (
            "https://raw.githubusercontent.com/alyosha-tas/gba-tests/" + self.manifest["revision"]
        )

    def main(self, *arguments):
        with patch(
            "sys.argv", ["check_serial_probe.py", "--directory", str(self.directory), *arguments]
        ):
            probe.main()

    @patch("check_serial_probe.subprocess.run")
    @patch("check_serial_probe.download_verified")
    def test_downloads_all_inputs_and_permission_notice(self, download, run):
        download.return_value = b"verified data"
        self.main("--fetch")
        self.assertEqual(
            download.call_args_list,
            [
                call(self.base + "/LICENSE", self.manifest["license_sha256"]),
                *[
                    call(self.base + "/" + case["source_path"], case["sha256"])
                    for case in self.manifest["cases"]
                ],
            ],
        )
        self.assertEqual(
            {p.name for p in self.directory.iterdir()},
            {"serial-probes-LICENSE.txt", *[c["path"] for c in self.manifest["cases"]]},
        )
        run.assert_not_called()

    @patch("check_serial_probe.download_verified")
    def test_case_selection_uses_original_path(self, download):
        download.return_value = b"verified data"
        self.main("--fetch", "--test", "halt_pc")
        self.assertEqual(download.call_count, 2)
        self.assertEqual(download.call_args.args[0], self.base + "/irq/halt_pc.gba")
        self.assertTrue((self.directory / "halt_pc.gba").is_file())

    @patch("check_serial_probe.download_verified")
    def test_unknown_case_never_downloads(self, download):
        with self.assertRaises(SystemExit):
            self.main("--fetch", "--test", "unknown")
        download.assert_not_called()
        self.assertFalse(self.directory.exists())

    @patch("check_serial_probe.subprocess.run")
    def test_execution_requires_runner_test_and_original_halt_bios(self, run):
        for arguments in [
            [],
            ["--test", "serial_time"],
            ["--runner", "unused"],
            ["--runner", "unused", "--test", "halt_pc"],
        ]:
            with self.assertRaises(SystemExit):
                self.main(*arguments)
        run.assert_not_called()

    @patch("check_serial_probe.download_verified")
    def test_existing_different_file_is_not_replaced(self, download):
        self.directory.mkdir()
        case = self.manifest["cases"][0]
        path = self.directory / case["path"]
        path.write_bytes(b"previous input")
        download.return_value = b"verified notice"
        with self.assertRaisesRegex(ValueError, "Existing serial probe checksum mismatch"):
            self.main("--fetch", "--test", case["name"])
        self.assertEqual(path.read_bytes(), b"previous input")
        self.assertEqual(download.call_count, 1)

    @patch("check_serial_probe.download_verified")
    def test_verified_existing_file_needs_no_image_download(self, download):
        self.directory.mkdir()
        data = b"verified existing cartridge"
        case = dict(self.manifest["cases"][0], sha256=hashlib.sha256(data).hexdigest())
        (self.directory / case["path"]).write_bytes(data)
        download.return_value = b"verified notice"
        probe.fetch_cases(self.manifest, [case], self.directory)
        self.assertEqual(download.call_count, 1)

    @patch("check_serial_probe.download_verified")
    def test_failed_download_does_not_publish_image(self, download):
        for error in [TimeoutError("transport failed"), SystemExit("checksum mismatch")]:
            download.side_effect = [b"verified notice", error]
            with self.assertRaises(type(error)):
                self.main("--fetch", "--test", "serial_time")
            self.assertFalse((self.directory / "serial_time.gba").exists())


class ObjectTailInputs(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name) / "inputs"
        self.manifest = json.loads(
            Path(object_probe.__file__).with_name("object-tail-probe.json").read_text()
        )
        self.case, self.data = ThumbComparisonProtocol().image(self.manifest["case"])
        self.manifest["case"] = self.case
        self.rom = self.directory / self.case["path"]

    def main(self, *arguments):
        with patch("check_object_tail_probe.json.loads", return_value=self.manifest), patch(
            "sys.argv",
            ["check_object_tail_probe.py", "--directory", str(self.directory), *arguments],
        ):
            object_probe.main()

    @patch("check_object_tail_probe.subprocess.run")
    @patch("check_object_tail_probe.download_verified")
    def test_verified_download_preserves_notice_and_input(self, download, run):
        download.side_effect = [b"license", self.data]
        self.main("--fetch")
        self.assertEqual(self.rom.read_bytes(), self.data)
        self.assertEqual((self.directory / "object-tail-LICENSE.txt").read_bytes(), b"license")
        base = (
            "https://raw.githubusercontent.com/alyosha-tas/gba-tests/" + self.manifest["revision"]
        )
        self.assertEqual(
            download.call_args_list,
            [
                call(base + "/LICENSE", self.manifest["license_sha256"]),
                call(base + "/" + self.case["path"], self.case["sha256"]),
            ],
        )
        run.assert_not_called()

    @patch("check_object_tail_probe.download_verified")
    def test_changed_existing_input_is_never_overwritten(self, download):
        self.rom.parent.mkdir(parents=True)
        self.rom.write_bytes(b"local input")
        download.side_effect = [b"license", self.data]
        with self.assertRaisesRegex(SystemExit, "Existing object-tail cartridge differs"):
            self.main("--fetch")
        self.assertEqual(self.rom.read_bytes(), b"local input")
        self.assertFalse((self.directory / "object-tail-LICENSE.txt").exists())

    @patch("check_object_tail_probe.download_verified")
    def test_failed_download_or_contract_never_publishes(self, download):
        for failure in [TimeoutError("transport failed"), b"wrong image"]:
            download.side_effect = [b"license", failure]
            with self.assertRaises((TimeoutError, ValueError)):
                self.main("--fetch")
            self.assertFalse(self.directory.exists())

    @patch("check_object_tail_probe.subprocess.run")
    def test_execution_requires_runner_and_verified_image(self, run):
        with self.assertRaises(SystemExit):
            self.main()
        self.rom.parent.mkdir(parents=True)
        self.rom.write_bytes(b"wrong image")
        with self.assertRaises(ValueError):
            self.main("--runner", "unused")
        run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
