"""Check publication, caching and failure handling for the display cartridge corpus."""

# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import call, patch

import check_display_probe as display_probe
from test_immediate_comparisons import comparison_image


class DisplayProbeInputs(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name) / "inputs"
        self.manifest = json.loads(
            Path(display_probe.__file__).with_name("display-probes.json").read_text()
        )
        self.images = {}
        for index, original in enumerate(self.manifest["cases"]):
            case, data = comparison_image(original)
            self.manifest["cases"][index] = case
            self.images[case["path"]] = data
        self.notice = b"owned license fixture"
        self.manifest["license_sha256"] = hashlib.sha256(self.notice).hexdigest()
        self.base = (
            "https://raw.githubusercontent.com/alyosha-tas/gba-tests/" + self.manifest["revision"]
        )

    def main(self, *arguments):
        with patch("check_display_probe.json.loads", return_value=self.manifest), patch(
            "sys.argv", ["check_display_probe.py", "--directory", str(self.directory), *arguments]
        ):
            display_probe.main()

    @patch("check_display_probe.subprocess.run")
    @patch("check_display_probe.download_verified")
    def test_all_downloads_are_checked_and_valid_cache_needs_no_network(self, download, run):
        download.side_effect = [self.notice] + [
            self.images[c["path"]] for c in self.manifest["cases"]
        ]
        self.main("--fetch")
        self.assertEqual(
            download.call_args_list,
            [
                call(self.base + "/LICENSE", self.manifest["license_sha256"]),
                *[call(self.base + "/" + c["path"], c["sha256"]) for c in self.manifest["cases"]],
            ],
        )
        for path, data in self.images.items():
            self.assertEqual((self.directory / path).read_bytes(), data)
        self.assertEqual((self.directory / "display-probes-LICENSE.txt").read_bytes(), self.notice)
        download.reset_mock()
        self.main("--fetch")
        download.assert_not_called()
        run.assert_not_called()

    @patch("check_display_probe.download_verified")
    def test_selected_case_uses_original_source_path(self, download):
        case = self.manifest["cases"][2]
        download.side_effect = [self.notice, self.images[case["path"]]]
        self.main("--fetch", "--test", case["name"])
        self.assertEqual(download.call_count, 2)
        self.assertEqual(download.call_args.args[0], self.base + "/" + case["path"])
        self.assertEqual(len(list(self.directory.rglob("*.gba"))), 1)

    @patch("check_display_probe.download_verified")
    def test_existing_changed_input_is_not_replaced(self, download):
        path = self.directory / self.manifest["cases"][0]["path"]
        path.parent.mkdir(parents=True)
        path.write_bytes(b"local input")
        with self.assertRaises(ValueError):
            self.main("--fetch")
        self.assertEqual(path.read_bytes(), b"local input")
        download.assert_not_called()

    @patch("check_display_probe.download_verified")
    def test_failed_or_changed_download_publishes_nothing(self, download):
        first = self.images[self.manifest["cases"][0]["path"]]
        for failure in [TimeoutError("transport failed"), b"wrong image"]:
            download.side_effect = [self.notice, first, failure]
            with self.assertRaises((TimeoutError, ValueError)):
                self.main("--fetch")
            self.assertFalse(self.directory.exists())

    @patch("check_display_probe.download_verified")
    def test_wrong_license_publishes_nothing(self, download):
        download.return_value = b"wrong notice"
        with self.assertRaisesRegex(ValueError, "license checksum"):
            self.main("--fetch")
        self.assertFalse(self.directory.exists())
        self.assertEqual(download.call_count, 1)

    @patch("check_display_probe.subprocess.run")
    @patch("check_display_probe.download_verified")
    def test_execution_requires_runner_and_selection(self, download, run):
        for arguments in [
            [],
            ["--test", "startup-line"],
            ["--runner", "unused"],
            ["--fetch", "--test", "unknown"],
        ]:
            with self.assertRaises(SystemExit):
                self.main(*arguments)
        download.assert_not_called()
        run.assert_not_called()

    @patch("check_display_probe.download_verified")
    def test_paths_cannot_leave_the_input_directory(self, download):
        for path in ["../outside.gba", str(Path(self.temporary.name) / "outside.gba"), "."]:
            manifest = copy.deepcopy(self.manifest)
            manifest["cases"][0]["path"] = path
            with self.assertRaises(ValueError):
                display_probe.fetch_cases(manifest, manifest["cases"], self.directory)
        download.assert_not_called()
        self.assertFalse(self.directory.exists())

    def test_publication_failure_cleans_staging_and_existing_input_is_preserved(self):
        path = self.directory / "new.gba"
        with patch.object(Path, "replace", side_effect=OSError("publication failed")):
            with self.assertRaises(OSError):
                display_probe.publish(path, b"verified bytes")
        self.assertEqual(list(self.directory.iterdir()), [])
        path.write_bytes(b"existing input")
        with self.assertRaises(ValueError):
            display_probe.publish(path, b"different input")
        self.assertEqual(path.read_bytes(), b"existing input")


if __name__ == "__main__":
    unittest.main()
