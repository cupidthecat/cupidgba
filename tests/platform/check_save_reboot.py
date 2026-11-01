#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Require all 32 independent reboot cases from two isolated native workers."""
import argparse
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import time

BUDGET = 10.0
CASE = re.compile(r"case=(\d+) thumb=([01])")
SUMMARY = re.compile(r"PASS reboot-worker thumb=([01]) cases=(\d+)")


def verify_worker(output, worker, returncode):
    if returncode != 0:
        raise ValueError(f"worker {worker} exited with {returncode}")
    cases, summaries = [], []
    for line in output.decode("utf-8", errors="replace").splitlines():
        if line.startswith("case="):
            match = CASE.fullmatch(line)
            if not match:
                raise ValueError(f"worker {worker} has an invalid case record")
            cases.append(tuple(map(int, match.groups())))
        elif line.startswith("PASS reboot-worker"):
            match = SUMMARY.fullmatch(line)
            if not match:
                raise ValueError(f"worker {worker} has an invalid summary")
            summaries.append(tuple(map(int, match.groups())))
    expected = [(case, worker) for case in range(worker, 32, 2)]
    if cases != expected:
        raise ValueError(f"worker {worker} has missing, duplicate, reordered or wrong-mode cases")
    if summaries != [(worker, 16)]:
        raise ValueError(f"worker {worker} did not finish all sixteen cases")


def self_test():
    def valid(worker):
        records = [f"case={case} thumb={worker}" for case in range(worker, 32, 2)]
        records.append(f"PASS reboot-worker thumb={worker} cases=16")
        return ("\n".join(records) + "\n").encode()
    for worker in (0, 1):
        verify_worker(valid(worker), worker, 0)
    good = valid(0)
    lines = good.splitlines(keepends=True)
    bad = [
        (good, 1),
        (b"", 0),
        (b"".join(lines[1:]), 0),
        (b"".join([lines[0], *lines]), 0),
        (b"".join([lines[1], lines[0], *lines[2:]]), 0),
        (good.replace(b"case=0 thumb=0", b"case=0 thumb=1"), 0),
        (good.replace(b"case=0 thumb=0", b"case=1 thumb=0"), 0),
        (b"".join(lines[:-1]), 0),
        (good.replace(b"cases=16", b"cases=0"), 0),
        (good + lines[-1], 0),
    ]
    for output, returncode in bad:
        try:
            verify_worker(output, 0, returncode)
        except ValueError:
            continue
        raise AssertionError("accepted an incomplete or invalid worker")
    print("PASS two complete worker controls and ten invalid-result controls")


def run_workers(runner, directory):
    runner = runner.resolve(strict=True)
    directory.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="save-reboot-", dir=directory))
    workers, results = [], {}
    deadline = time.monotonic() + BUDGET
    try:
        for worker, mode in enumerate(("reboot-arm", "reboot-thumb")):
            work = root / str(worker)
            work.mkdir()
            process = subprocess.Popen([str(runner), mode], cwd=work,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            workers.append((worker, work, process))
        for worker, work, process in workers:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("reboot workers reached the ten-second group limit")
            output, errors = process.communicate(timeout=remaining)
            results[worker] = (process.returncode, output, errors)
            (work / "stdout.txt").write_bytes(output)
            (work / "stderr.txt").write_bytes(errors)
            verify_worker(output, worker, process.returncode)
        if time.monotonic() > deadline:
            raise TimeoutError("reboot workers reached the ten-second group limit")
    finally:
        # Stop only this invocation's children. Keep their logs and directories,
        # including partial output when either worker fails or times out.
        for worker, work, process in workers:
            if process.poll() is None:
                process.kill()
            if worker not in results:
                output, errors = process.communicate()
                results[worker] = (process.returncode, output, errors)
                (work / "stdout.txt").write_bytes(output)
                (work / "stderr.txt").write_bytes(errors)
        for worker in sorted(results):
            _, output, errors = results[worker]
            sys.stdout.buffer.write(output)
            sys.stderr.buffer.write(errors)
        print(f"worker records: {root}", flush=True)
    print("PASS 32 pending EEPROM/Flash operations through ARM/Thumb HardReset and clock wrap")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--directory", type=Path, default=Path.cwd())
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.runner is None:
        parser.error("--runner is required")
    # CTest retains the outer ten-second process guard. This also cleans up
    # workers when a parent receives a catchable termination signal.
    def interrupted(signum, _frame):
        raise RuntimeError(f"reboot worker checker interrupted by signal {signum}")
    signal.signal(signal.SIGTERM, interrupted)
    try:
        run_workers(args.runner, args.directory)
    except (ValueError, OSError, TimeoutError, subprocess.TimeoutExpired, RuntimeError) as error:
        print(f"FAIL reboot workers: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
