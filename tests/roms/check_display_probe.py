#!/usr/bin/env python3
"""Require the original display startup, IRQ, HALT and texture-tail comparisons."""

# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from fetch_inputs import download_verified
from protocols.immediate import require_result, result_register, verify_input


def input_path(directory, name):
    root = directory.resolve()
    path = (root / name).resolve()
    if not path.is_relative_to(root) or path == root:
        raise ValueError("Display input path leaves its directory")
    return path


def publish(path, data):
    if path.exists():
        if path.read_bytes() != data:
            raise ValueError("Existing display input differs from the verified data")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            dir=path.parent, prefix=".display-probe-", delete=False
        ) as output:
            temporary = Path(output.name)
            output.write(data)
        temporary.replace(path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def fetch_cases(manifest, cases, directory):
    base = "https://raw.githubusercontent.com/alyosha-tas/gba-tests/" + manifest["revision"]
    pending = []
    # Validate existing cartridges before downloading or publishing anything.
    for case in cases:
        path = input_path(directory, case["path"])
        if path.exists():
            verify_input(path.read_bytes(), case)
        else:
            pending.append((path, case))
    notice_path = input_path(directory, "display-probes-LICENSE.txt")
    notice = (
        notice_path.read_bytes()
        if notice_path.exists()
        else download_verified(base + "/LICENSE", manifest["license_sha256"])
    )
    if hashlib.sha256(notice).hexdigest() != manifest["license_sha256"]:
        raise ValueError("Display input license checksum mismatch")
    files = [(notice_path, notice)]
    for path, case in pending:
        data = download_verified(base + "/" + case["path"], case["sha256"])
        verify_input(data, case)
        files.append((path, data))
    # A failed download leaves the existing corpus intact. Each new file is
    # published only after all requested bytes and comparison contracts pass.
    for path, data in files:
        publish(path, data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--bios", type=Path)
    manifest = json.loads(Path(__file__).with_name("display-probes.json").read_text())
    parser.add_argument("--test", choices=[case["name"] for case in manifest["cases"]])
    parser.add_argument("--fetch", action="store_true")
    args = parser.parse_args()
    cases = [case for case in manifest["cases"] if not args.test or case["name"] == args.test]
    if args.fetch:
        fetch_cases(manifest, cases, args.directory)
        return
    if not args.runner or not args.test:
        parser.error("Execution requires --runner and --test")
    case = cases[0]
    rom = input_path(args.directory, case["path"])
    verify_input(rom.read_bytes(), case)
    command = [
        str(args.runner.resolve()),
        str(rom),
        "--frames",
        "360",
        "--steps",
        "150000000",
        "--expect-loop-register",
        str(result_register(case)),
        "--checkpoint-address",
        case["checkpoint"],
        "--terminal-address",
        case["terminal"],
    ]
    for observation in case["observations"]:
        command += [
            "--expect-register-at",
            observation["address"],
            str(observation["register"]),
            observation["value"],
        ]
    if args.bios:
        command += ["--bios", str(args.bios.resolve())]
    result = subprocess.run(command, capture_output=True, text=True, timeout=90)
    print(result.stdout, end="")
    print(result.stderr, end="")
    require_result(result.stdout, result.returncode, case)
    print(f"PASS unchanged display probe: {args.test}")


if __name__ == "__main__":
    main()
