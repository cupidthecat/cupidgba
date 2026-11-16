#!/usr/bin/env python3
# @file      check_format.py
# @author    cupidthecat
# @brief     Check tracked C source and headers against the repository clang-format style.
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
"""Check tracked C source and headers against the repository clang-format style."""

# SPDX-License-Identifier: GPL-3.0-or-later
import os
from pathlib import Path
import subprocess

from check_comment_spacing import check_files


def main():
    root = Path(__file__).resolve().parents[2]
    tracked = subprocess.check_output(["git", "ls-files", "-z"], cwd=root).decode().split("\0")
    files = [
        name
        for name in tracked
        if name.endswith((".c", ".h")) or (name.startswith("tests/") and name.endswith(".inc"))
    ]
    if not files:
        raise SystemExit("No tracked C files found")
    try:
        check_files(root, files)
    except ValueError as error:
        raise SystemExit(str(error)) from error
    formatter = os.environ.get("CLANG_FORMAT", "clang-format")
    subprocess.run([formatter, "--dry-run", "--Werror", *files], cwd=root, check=True)
    print(f"PASS clang-format: {len(files)} tracked C source, header and fixture files")


if __name__ == "__main__":
    main()
