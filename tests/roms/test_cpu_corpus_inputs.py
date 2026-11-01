#!/usr/bin/env python3
"""Check CPU corpus input integrity without downloading or executing a ROM."""
# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import hashlib
import os
from pathlib import Path
import stat
import struct
import tempfile
import unittest
from unittest.mock import patch

from check_cpu_corpus import main, store_input, verify_input


class InputChecks(unittest.TestCase):
    def setUp(self):
        # This owned layout tests validation; it is not an altered corpus ROM.
        self.data = bytearray(128)
        struct.pack_into('<I', self.data, 0, 0xE1A00000)
        struct.pack_into('<I', self.data, 60, 3)
        self.manifest = {'bytes': 128, 'table_offset': 64, 'cases_per_rom': 3,
                         'control_words': [{'address': '08000000', 'value': 'e1a00000'}]}
        self.case = {'sha256': hashlib.sha256(self.data).hexdigest()}

    def test_complete_input(self):
        verify_input(self.data, self.manifest, self.case)

    def test_bad_length_or_digest(self):
        for data in (self.data[:-1], self.data + b'x', bytes(128)):
            with self.subTest(bytes=len(data)), self.assertRaises(SystemExit):
                verify_input(data, self.manifest, self.case)

    def test_layout_and_count(self):
        for key, value in [('control_words', [{'address': '08000000', 'value': 'eafffffe'}]),
                           ('control_words', [{'address': '07fffffc', 'value': 'e1a00000'}]),
                           ('control_words', [{'address': '08000100', 'value': 'e1a00000'}]),
                           ('cases_per_rom', 4), ('table_offset', 0), ('table_offset', 132)]:
            manifest = copy.deepcopy(self.manifest)
            manifest[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(SystemExit):
                verify_input(self.data, manifest, self.case)

    def test_existing_inputs_are_preserved(self):
        with tempfile.TemporaryDirectory(prefix='cupidgba-corpus-input-') as directory:
            path = Path(directory) / 'input'
            store_input(path, self.data, self.case['sha256'])
            before = path.stat().st_mtime_ns
            path.chmod(stat.S_IREAD)
            try:
                store_input(path, self.data, self.case['sha256'])
                self.assertEqual(path.read_bytes(), self.data)
                self.assertEqual(path.stat().st_mtime_ns, before)
            finally:
                path.chmod(stat.S_IREAD | stat.S_IWRITE)
            path.write_bytes(b'unrecognized existing file')
            with self.assertRaises(SystemExit):
                store_input(path, self.data, self.case['sha256'])
            self.assertEqual(path.read_bytes(), b'unrecognized existing file')
            folder = Path(directory) / 'folder'
            folder.mkdir()
            with self.assertRaises(SystemExit):
                store_input(folder, self.data, self.case['sha256'])
            if hasattr(os, 'mkfifo'):
                fifo = Path(directory) / 'fifo'
                os.mkfifo(fifo)
                with self.assertRaises(SystemExit):
                    store_input(fifo, self.data, self.case['sha256'])

    def test_rejected_input_never_starts_runner(self):
        with tempfile.TemporaryDirectory(prefix='cupidgba-corpus-reject-') as directory:
            root = Path(directory)
            (root / 'ARM_Any.gba').write_bytes(b'owned invalid input')
            argv = ['check_cpu_corpus.py', '--directory', str(root), '--test', 'arm-any',
                    '--runner', str(root / 'never-started')]
            with patch('sys.argv', argv), patch('check_cpu_corpus.subprocess.run') as run:
                with self.assertRaises(SystemExit):
                    main()
                run.assert_not_called()
            self.assertEqual(sorted(path.name for path in root.iterdir()), ['ARM_Any.gba'])


if __name__ == '__main__':
    unittest.main()
