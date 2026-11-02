# @file test_fifo_source_builder.py
# @brief Keep failed or altered source builds from replacing verified test inputs.
"""Keep failed or altered source builds from replacing verified test inputs."""

# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from build_fifo_source import build


def digest(data):
    return hashlib.sha256(data).hexdigest()


class FifoSourceBuilder(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.contents = {
            "fifo_dma/fifo.asm": b"original source\n",
            "lib/constants.inc": b"original constants\n",
        }
        for name, data in self.contents.items():
            path = self.source / name
            path.parent.mkdir(exist_ok=True)
            path.write_bytes(data)
        self.hashes = {name: digest(data) for name, data in self.contents.items()}
        self.binary = b"complete verified cartridge"
        self.case = {"sha256": digest(self.binary)}
        self.output = self.root / "output/fifo-source.gba"
        self.assembler = self.root / "assembler"

    def run_build(self):
        return build(self.source, self.assembler, self.output, self.hashes, self.case)

    def assemble(self, arguments, *, check, timeout):
        self.assertTrue(check)
        self.assertEqual(timeout, 20)
        self.assertEqual(arguments[0], str(self.assembler))
        copied = Path(arguments[1]).parent.parent
        self.assertNotEqual(copied, self.source)
        self.assertEqual(copied.parent, self.output.parent)
        for name, data in self.contents.items():
            self.assertEqual((copied / name).read_bytes().replace(b"\r", b""), data)
        Path(arguments[2]).write_bytes(self.binary)

    def assert_clean(self):
        for name, data in self.contents.items():
            self.assertEqual((self.source / name).read_bytes().replace(b"\r", b""), data)
        if self.output.parent.exists():
            self.assertEqual(list(self.output.parent.glob("cupid-fifo-source-*")), [])

    def test_verified_build_isolated_and_complete(self):
        with patch("build_fifo_source.subprocess.run", side_effect=self.assemble) as run:
            self.assertEqual(self.run_build(), len(self.binary))
            run.assert_called_once()
        self.assertEqual(self.output.read_bytes(), self.binary)
        self.assert_clean()

    def test_carriage_returns_only_are_ignored(self):
        for name, data in self.contents.items():
            (self.source / name).write_bytes(data.replace(b"\n", b"\r\n"))
        with patch("build_fifo_source.subprocess.run", side_effect=self.assemble):
            self.run_build()
        self.assert_clean()

    def test_every_changed_source_rejected_before_assembly(self):
        for name, data in self.contents.items():
            path = self.source / name
            path.write_bytes(data + b"changed")
            with patch("build_fifo_source.subprocess.run") as run:
                with self.assertRaisesRegex(ValueError, "source checksum mismatch"):
                    self.run_build()
                run.assert_not_called()
            self.assertFalse(self.output.exists())
            path.write_bytes(data)

    def test_source_output_and_symlink_alias_refused(self):
        alias = self.root / "alias"
        alias.symlink_to(self.source, target_is_directory=True)
        for output in [self.source, self.source / "new.gba", alias / "new.gba"]:
            self.output = output
            with patch("build_fifo_source.subprocess.run") as run:
                with self.assertRaisesRegex(ValueError, "outside the source"):
                    self.run_build()
                run.assert_not_called()

    def test_different_existing_file_is_untouched(self):
        self.output.parent.mkdir()
        self.output.write_bytes(b"user data")
        with patch("build_fifo_source.subprocess.run") as run:
            with self.assertRaisesRegex(ValueError, "different file"):
                self.run_build()
            run.assert_not_called()
        self.assertEqual(self.output.read_bytes(), b"user data")

    def test_matching_existing_image_is_rebuilt(self):
        self.output.parent.mkdir()
        self.output.write_bytes(self.binary)
        with patch("build_fifo_source.subprocess.run", side_effect=self.assemble) as run:
            self.run_build()
            run.assert_called_once()
        self.assertEqual(self.output.read_bytes(), self.binary)
        self.assert_clean()

    def test_bad_output_never_replaces_existing_image(self):
        def wrong(arguments, **kwargs):
            Path(arguments[2]).write_bytes(b"wrong or partial cartridge")

        for exists in [False, True]:
            if exists:
                self.output.write_bytes(self.binary)
            with patch("build_fifo_source.subprocess.run", side_effect=wrong):
                with self.assertRaisesRegex(ValueError, "cartridge checksum mismatch"):
                    self.run_build()
            self.assertEqual(self.output.exists(), exists)
            if exists:
                self.assertEqual(self.output.read_bytes(), self.binary)
            self.assert_clean()

    def test_assembler_failure_timeout_or_missing_output(self):
        self.output.parent.mkdir()
        self.output.write_bytes(self.binary)
        for failure in [
            subprocess.CalledProcessError(1, ["assembler"]),
            subprocess.TimeoutExpired(["assembler"], 20),
            None,
        ]:
            with patch("build_fifo_source.subprocess.run", side_effect=failure):
                with self.assertRaises(
                    (subprocess.CalledProcessError, subprocess.TimeoutExpired, FileNotFoundError)
                ):
                    self.run_build()
            self.assertEqual(self.output.read_bytes(), self.binary)
            self.assert_clean()


if __name__ == "__main__":
    unittest.main()
