#!/usr/bin/env python3
"""Require the runner to retain a result before later guest code overwrites it."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    runner = parser.parse_args().runner.resolve()
    with tempfile.TemporaryDirectory(prefix='cupidgba-checkpoint-') as folder:
        rom = Path(folder) / 'owned.gba'

        def run(first, final, checkpoint='0x08000004', enabled=True):
            image = [0xE3A0C000 | first, 0xE1A0C00C, 0xE3A0C000 | final, 0xEAFFFFFE]
            rom.write_bytes(struct.pack('<4I', *image) + bytes(240))
            command = [str(runner), str(rom), '--frames', '1', '--steps', '10000',
                       '--expect-loop-register', '12', '--terminal-address', '0x0800000C']
            if enabled:
                command += ['--checkpoint-address', checkpoint]
            return subprocess.run(command, text=True, capture_output=True, timeout=3)

        accepted = run(0, 0)
        if accepted.returncode or 'checkpoint=yes r12=0 result=PASS' not in accepted.stdout.splitlines():
            raise SystemExit('Runner did not accept the owned passing checkpoint')
        masked = run(42, 0)
        if masked.returncode != 1 or 'checkpoint=yes r12=42 result=FAIL' not in masked.stdout.splitlines():
            raise SystemExit('A later zero concealed the earlier guest failure')
        if 'terminal=yes r12=0 result=PASS' not in masked.stdout.splitlines():
            raise SystemExit('Negative control did not reach its overwritten terminal')
        legacy = run(42, 0, enabled=False)
        if legacy.returncode or 'checkpoint=' in legacy.stdout:
            raise SystemExit('Optional checkpoint changed ordinary runner behavior')
        terminal = run(0, 9)
        if terminal.returncode != 1 or 'terminal=yes r12=9 result=FAIL' not in terminal.stdout.splitlines():
            raise SystemExit('Passing checkpoint concealed a terminal failure')
        missing = run(0, 0, checkpoint='0x08000040')
        if missing.returncode != 1 or 'checkpoint=no r12=0 result=MISSING' not in missing.stdout.splitlines():
            raise SystemExit('Runner accepted an unreachable checkpoint')
        invalid = run(0, 0, checkpoint='0')
        if invalid.returncode != 2:
            raise SystemExit('Runner accepted a zero checkpoint address')
        invalid = subprocess.run([str(runner), str(rom), '--checkpoint-address', '0x08000004'],
                                 text=True, capture_output=True, timeout=3)
        if invalid.returncode != 2:
            raise SystemExit('Runner accepted a checkpoint without a result register')
    print('PASS checkpoint retention, overwritten/missing results and ordinary terminals')


if __name__ == '__main__':
    main()
