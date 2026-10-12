#!/usr/bin/env python3
"""Verify a pinned input before running its specified result protocol."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true")
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--test")
    args = parser.parse_args()
    manifest = json.loads(Path(__file__).with_name("manifest.json").read_text())
    cases = manifest["roms"]
    if args.test:
        cases = [case for case in cases if case["name"] == args.test]
    if not cases:
        raise SystemExit("No matching ROM test")
    if not args.fetch and not args.runner:
        parser.error("--runner is required unless fetching inputs")
    for case in cases:
        path = args.directory / case["path"]
        if args.fetch:
            url = f'https://raw.githubusercontent.com/jsmolka/gba-tests/{manifest["revision"]}/{case["path"]}'
            data = urllib.request.urlopen(url, timeout=30).read()
        else:
            data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != case["sha256"]:
            raise SystemExit(f'Input checksum mismatch: {case["name"]}')
        if args.fetch:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            print(f'Fetched verified input: {case["name"]}')
        else:
            command = [str(args.runner.resolve()), str(path.resolve()), "--expect-loop-register",
                       str(case["register"]), "--terminal-address", case["terminal_address"]]
            subprocess.run(command, timeout=45, check=True)


if __name__ == "__main__":
    main()
