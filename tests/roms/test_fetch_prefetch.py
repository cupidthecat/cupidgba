#!/usr/bin/env python3
"""Check pinned prefetch input selection and reject failed or altered downloads."""
# SPDX-License-Identifier: GPL-3.0-or-later
import faulthandler
import hashlib
import http.client
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import call, patch

import check_prefetch_probe as probe


class PrefetchInputs(unittest.TestCase):
    revision = '66f5f1d60cd7030b600606886c76f5c821ecaf9d'
    source = f'https://raw.githubusercontent.com/jsmolka/gba-tests/{revision}'
    notice = b'input permission notice\n'
    image = b'complete test cartridge\n'

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name) / 'inputs'
        self.manifest = {
            'revision': self.revision,
            'license_sha256': hashlib.sha256(self.notice).hexdigest(),
            'cases': [{'name': 'test', 'path': 'prefetcher/test.gba',
                       'sha256': hashlib.sha256(self.image).hexdigest()}],
        }

    def main(self, *arguments):
        with patch('sys.argv', ['check_prefetch_probe.py', '--directory',
                                str(self.directory), *arguments]):
            probe.main()

    @patch('check_prefetch_probe.subprocess.run')
    @patch('check_prefetch_probe.download_verified')
    def test_fetches_every_pinned_case_and_permission_notice(self, download, run):
        manifest = json.loads(Path(probe.__file__).with_name('prefetch-probes.json').read_text())
        self.assertEqual(manifest['revision'], self.revision)
        self.assertEqual(len(manifest['cases']), 20)
        download.return_value = self.image
        self.main('--fetch')
        self.assertEqual(download.call_args_list, [
            call(f'{self.source}/LICENSE', manifest['license_sha256']),
            *[call(f'{self.source}/{case["path"]}', case['sha256']) for case in manifest['cases']],
        ])
        expected = {'prefetch-LICENSE.txt', *[case['path'] for case in manifest['cases']]}
        actual = {path.relative_to(self.directory).as_posix()
                  for path in self.directory.rglob('*') if path.is_file()}
        self.assertEqual(actual, expected)
        self.assertTrue(all(path.read_bytes() == self.image
                            for path in self.directory.rglob('*') if path.is_file()))
        run.assert_not_called()

    @patch('check_prefetch_probe.subprocess.run')
    @patch('check_prefetch_probe.download_verified')
    def test_selecting_one_case_keeps_its_original_path(self, download, run):
        download.side_effect = [self.notice, self.image]
        self.main('--fetch', '--test', 'boundary-legacy')
        self.assertEqual(download.call_count, 2)
        self.assertEqual(download.call_args.args[0], f'{self.source}/prefetcher/bounday_test_1.gba')
        self.assertEqual((self.directory / 'prefetch-LICENSE.txt').read_bytes(), self.notice)
        self.assertEqual((self.directory / 'prefetcher/bounday_test_1.gba').read_bytes(), self.image)
        run.assert_not_called()

    @patch('check_prefetch_probe.download_verified')
    def test_unknown_case_fails_before_downloading(self, download):
        with self.assertRaises(SystemExit) as result:
            self.main('--fetch', '--test', 'missing')
        self.assertEqual(result.exception.code, 2)
        self.assertFalse(self.directory.exists())
        download.assert_not_called()

    @patch('check_prefetch_probe.subprocess.run')
    def test_execution_still_requires_runner_and_case(self, run):
        for arguments in ([], ['--test', 'boundary-1'], ['--runner', 'unused']):
            with self.subTest(arguments=arguments), self.assertRaises(SystemExit) as result:
                self.main(*arguments)
            self.assertEqual(result.exception.code, 2)
        run.assert_not_called()

    @patch('fetch_inputs.urllib.request.urlopen')
    def test_altered_notice_fails_before_writing_inputs(self, open_url):
        open_url.return_value = io.BytesIO(b'changed notice')
        with self.assertRaisesRegex(SystemExit, 'checksum mismatch'):
            probe.fetch_cases(self.manifest, self.manifest['cases'], self.directory)
        self.assertFalse(self.directory.exists())
        self.assertEqual(open_url.call_count, 1)

    @patch('fetch_inputs.urllib.request.urlopen')
    def test_altered_image_does_not_replace_existing_file(self, open_url):
        path = self.directory / self.manifest['cases'][0]['path']
        path.parent.mkdir(parents=True)
        path.write_bytes(self.image)
        open_url.side_effect = [io.BytesIO(self.notice), io.BytesIO(b'changed image')]
        with self.assertRaisesRegex(SystemExit, 'checksum mismatch'):
            probe.fetch_cases(self.manifest, self.manifest['cases'], self.directory)
        self.assertEqual(path.read_bytes(), self.image)
        self.assertEqual((self.directory / 'prefetch-LICENSE.txt').read_bytes(), self.notice)
        self.assertEqual(open_url.call_count, 2)

    @patch('fetch_inputs.time.sleep')
    @patch('fetch_inputs.urllib.request.urlopen')
    def test_partial_transfer_retries_whole_image(self, open_url, sleep):
        partial = io.BytesIO()
        partial.read = lambda: (_ for _ in ()).throw(http.client.IncompleteRead(b'partial', 20))
        open_url.side_effect = [io.BytesIO(self.notice), partial, io.BytesIO(self.image)]
        probe.fetch_cases(self.manifest, self.manifest['cases'], self.directory)
        self.assertEqual((self.directory / self.manifest['cases'][0]['path']).read_bytes(), self.image)
        self.assertEqual(open_url.call_count, 3)
        self.assertEqual(sleep.call_args_list, [call(1)])

    @patch('fetch_inputs.time.sleep')
    @patch('fetch_inputs.urllib.request.urlopen')
    def test_exhausted_transport_writes_no_partial_image(self, open_url, sleep):
        open_url.side_effect = [io.BytesIO(self.notice), TimeoutError('timeout'),
                                TimeoutError('timeout'), TimeoutError('timeout')]
        with self.assertRaises(TimeoutError):
            probe.fetch_cases(self.manifest, self.manifest['cases'], self.directory)
        self.assertFalse((self.directory / self.manifest['cases'][0]['path']).exists())
        self.assertEqual(open_url.call_count, 4)
        self.assertEqual(sleep.call_args_list, [call(1), call(2)])


if __name__ == '__main__':
    faulthandler.dump_traceback_later(8)
    try:
        unittest.main()
    finally:
        faulthandler.cancel_dump_traceback_later()
