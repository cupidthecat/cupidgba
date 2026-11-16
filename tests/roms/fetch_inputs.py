# @file      fetch_inputs.py
# @author    cupidthecat
# @brief     Download immutable test inputs without accepting incomplete or altered data.
# @date      2026-11-16
#
# @copyright Copyright (c) 2026 cupidthecat
#
# This file is part of CupidGBA.
#
# CupidGBA is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# CupidGBA is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with CupidGBA.  If not, see <https://gnu.org>.
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
