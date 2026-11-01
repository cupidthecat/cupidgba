#!/usr/bin/env python3
"""Check the effective optimization of every configured compiler command."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import json
from pathlib import Path
import re
import shlex


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compile-commands", type=Path, required=True)
    parser.add_argument("--optimization", choices=("none", "O1"), required=True)
    args = parser.parse_args()
    commands = json.loads(args.compile_commands.read_text())
    if not commands:
        raise SystemExit("No compiler commands to verify")
    for entry in commands:
        tokens = entry.get("arguments") or shlex.split(entry["command"])
        levels = [token for token in tokens if re.fullmatch(r"-O(?:\d+|[sgz]|fast)?", token)]
        valid = all(level == "-O0" for level in levels) if args.optimization == "none" else levels and levels[-1] == "-O1"
        if not valid:
            raise SystemExit(f"Unexpected optimization for {entry['file']}: {levels}")
    print(f"Verified optimization {args.optimization} in {len(commands)} compiler commands")


if __name__ == "__main__":
    main()
