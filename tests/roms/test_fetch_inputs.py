#!/usr/bin/env python3
"""Fault-inject transport failures and verify test input integrity enforcement."""
# SPDX-License-Identifier: GPL-3.0-or-later
import faulthandler

# Capture a blocked import or test before CTest's existing ten-second limit.
if __name__ == "__main__":
    faulthandler.dump_traceback_later(8)

import hashlib
import http.client
import io
import unittest
from unittest.mock import patch
import urllib.error

from fetch_inputs import download_verified


class Downloads(unittest.TestCase):
    data = b"verified test data"
    digest = hashlib.sha256(data).hexdigest()
    url = "https://example.invalid/pinned-test"

    @patch("fetch_inputs.time.sleep")
    @patch("fetch_inputs.urllib.request.urlopen")
    def test_partial_and_timed_out_transfers_retry_full_input(self, open_url, sleep):
        partial = io.BytesIO()
        partial.read = lambda: (_ for _ in ()).throw(http.client.IncompleteRead(b"partial", 20))
        open_url.side_effect = [partial, urllib.error.URLError("timeout"), io.BytesIO(self.data)]
        self.assertEqual(download_verified(self.url, self.digest), self.data)
        self.assertEqual(open_url.call_count, 3)
        self.assertEqual([call.args for call in sleep.call_args_list], [(1,), (2,)])

    @patch("fetch_inputs.time.sleep")
    @patch("fetch_inputs.urllib.request.urlopen")
    def test_transport_failure_stops_after_three_attempts(self, open_url, sleep):
        open_url.side_effect = TimeoutError("connection timed out")
        with self.assertRaises(TimeoutError):
            download_verified(self.url, self.digest)
        self.assertEqual(open_url.call_count, 3)
        self.assertEqual(sleep.call_count, 2)

    @patch("fetch_inputs.time.sleep")
    @patch("fetch_inputs.urllib.request.urlopen")
    def test_changed_payload_fails_without_retry(self, open_url, sleep):
        open_url.return_value = io.BytesIO(b"wrong contents")
        with self.assertRaisesRegex(SystemExit, "checksum mismatch"):
            download_verified(self.url, self.digest)
        self.assertEqual(open_url.call_count, 1)
        sleep.assert_not_called()

    @patch("fetch_inputs.time.sleep")
    @patch("fetch_inputs.urllib.request.urlopen")
    def test_http_server_failure_retries_but_missing_input_fails(self, open_url, sleep):
        error = urllib.error.HTTPError(self.url, 503, "unavailable", {}, None)
        open_url.side_effect = [error, io.BytesIO(self.data)]
        self.assertEqual(download_verified(self.url, self.digest), self.data)
        self.assertEqual(open_url.call_count, 2)
        open_url.reset_mock()
        sleep.reset_mock()
        open_url.side_effect = urllib.error.HTTPError(self.url, 404, "missing", {}, None)
        with self.assertRaises(urllib.error.HTTPError):
            download_verified(self.url, self.digest)
        self.assertEqual(open_url.call_count, 1)
        sleep.assert_not_called()


if __name__ == "__main__":
    try:
        unittest.main()
    finally:
        faulthandler.cancel_dump_traceback_later()
