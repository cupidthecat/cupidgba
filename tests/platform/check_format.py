#!/usr/bin/env python3
"""Check tracked C source and headers against the repository clang-format style."""

# SPDX-License-Identifier: GPL-3.0-or-later
import os
from pathlib import Path
import subprocess


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
    formatter = os.environ.get("CLANG_FORMAT", "clang-format")
    subprocess.run([formatter, "--dry-run", "--Werror", *files], cwd=root, check=True)
    print(f"PASS clang-format: {len(files)} tracked C source, header and fixture files")


if __name__ == "__main__":
    main()
