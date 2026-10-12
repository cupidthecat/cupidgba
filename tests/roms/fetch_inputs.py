"""Download immutable test inputs without accepting incomplete or altered data."""
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import http.client
import time
import urllib.error
import urllib.request


def download_verified(url, digest):
    for attempt in range(3):
        try:
            with urllib.request.urlopen(url, timeout=30) as response:
                data = response.read()
        except (urllib.error.URLError, http.client.IncompleteRead, OSError) as error:
            if isinstance(error, urllib.error.HTTPError) and error.code not in (429, 500, 502, 503, 504):
                raise
            if attempt == 2:
                raise
            print(f"Test input transport failed; retry {attempt + 1}/2", flush=True)
            time.sleep(attempt + 1)
            continue
        if hashlib.sha256(data).hexdigest() != digest:
            raise SystemExit("Downloaded test input checksum mismatch")
        return data
    raise AssertionError("Unreachable download state")
