#!/usr/bin/env python3
"""Reject zero-result terminals that did not satisfy their guest comparisons."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    runner = parser.parse_args().runner.resolve()
    with tempfile.TemporaryDirectory(prefix='cupidgba-observations-') as folder:
        rom = Path(folder) / 'owned.gba'

        def image(thumb=False, value=0x4C, repeats=None):
            data = bytearray(256)
            if repeats is not None:
                # MOV r12,0; MOV r0,4C; MOV r2,count; CMP r0,4C;
                # SUBS r2,1; BNE comparison; MOV r12,r12; terminal.
                words = [0xE3A0C000, 0xE3A0004C, 0xE3A02000 | repeats,
                         0xE350004C, 0xE2522001, 0x1AFFFFFC, 0xE1A0C00C, 0xEAFFFFFE]
                struct.pack_into('<8I', data, 0, *words)
                checkpoint, terminal = 0x08000018, 0x0800001C
                probes = [(0x0800000C, 0, 0x4C)] * repeats
            elif thumb:
                # Select an owned Thumb stream, compare r0/r1, then return
                # to an ARM terminal. No comparison writes r12 on failure.
                struct.pack_into('<7I', data, 0, 0xE3A0C000, 0xE3A00000 | value,
                                 0xE3A0106A, 0xE59F2004, 0xE12FFF12, 0xEAFFFFFE, 0x08000021)
                struct.pack_into('<4H', data, 0x20, 0x284C, 0x296A, 0x4A02, 0x4710)
                struct.pack_into('<I', data, 0x30, 0x08000040)
                struct.pack_into('<I', data, 0x40, 0xEAFFFFFE)
                checkpoint = terminal = 0x08000040
                probes = [(0x08000020, 0, 0x4C), (0x08000022, 1, 0x6A)]
            else:
                # Both comparison outcomes bypass the failure-recording MOV
                # at14h. The bad input therefore has r12=0 at both boundaries.
                words = [0xE3A0C000, 0xE3A00000 | value, 0xE350004C, 0x0A000001,
                         0xEA000000, 0xE1A0C000, 0xE3A0106A, 0xE351006A, 0xE1A0C00C, 0xEAFFFFFE]
                struct.pack_into('<10I', data, 0, *words)
                checkpoint, terminal = 0x08000020, 0x08000024
                probes = [(0x08000008, 0, 0x4C), (0x0800001C, 1, 0x6A)]
            rom.write_bytes(data)
            return checkpoint, terminal, probes

        def run(checkpoint, terminal, probes=(), extra=()):
            command = [str(runner), str(rom), '--frames', '1', '--steps', '10000',
                       '--expect-loop-register', '12', '--checkpoint-address', hex(checkpoint),
                       '--terminal-address', hex(terminal)]
            for address, reg, expected in probes:
                command += ['--expect-register-at', hex(address), str(reg), hex(expected)]
            return subprocess.run(command + list(extra), text=True, capture_output=True, timeout=3)

        for thumb in (False, True):
            checkpoint, terminal, probes = image(thumb)
            passed = run(checkpoint, terminal, probes)
            assert passed.returncode == 0 and 'observations=2/2 result=PASS' in passed.stdout, passed.stdout + passed.stderr
            records = [line for line in passed.stdout.splitlines() if line.startswith('observation=')]
            expected_mode = 'thumb' if thumb else 'arm'
            opcodes = (0x284C, 0x296A) if thumb else (0xE350004C, 0xE351006A)
            for index, ((address, reg, value), opcode) in enumerate(zip(probes, opcodes)):
                assert records[index] == (f'observation={index} pc={address:08x} mode={expected_mode} opcode={opcode:08x} '
                                          f'r{reg}={value:08x} expected={value:08x} result=PASS')
            checkpoint, terminal, probes = image(thumb, value=0x44)
            legacy = run(checkpoint, terminal)
            assert legacy.returncode == 0 and 'terminal=yes r12=0 result=PASS' in legacy.stdout
            rejected = run(checkpoint, terminal, probes)
            assert rejected.returncode == 1 and 'r0=00000044 expected=0000004c result=FAIL' in rejected.stdout
            assert 'checkpoint=yes r12=0 result=PASS' in rejected.stdout and 'terminal=yes r12=0 result=PASS' in rejected.stdout
            assert 'observations=2/2 result=FAIL' in rejected.stdout

        checkpoint, terminal, probes = image()
        missing = run(checkpoint, terminal, probes + [(0x08000044, 0, 0)])
        assert missing.returncode == 1 and 'observations=2/3 result=MISSING' in missing.stdout
        unissued = run(checkpoint, terminal, probes + [(terminal, 12, 0)])
        assert unissued.returncode == 1 and 'observations=2/3 result=MISSING' in unissued.stdout
        reordered = run(checkpoint, terminal, list(reversed(probes)))
        assert reordered.returncode == 1 and 'observation-order=FAIL' in reordered.stdout
        checkpoint, terminal, probes = image(repeats=2)
        repeated = run(checkpoint, terminal, probes)
        assert repeated.returncode == 0 and 'observations=2/2 result=PASS' in repeated.stdout
        extra = run(checkpoint, terminal, probes[:1])
        assert extra.returncode == 1 and 'observation-order=FAIL' in extra.stdout
        missing = run(checkpoint, terminal, probes + probes[:1])
        assert missing.returncode == 1 and 'observations=2/3 result=MISSING' in missing.stdout
        checkpoint, terminal, probes = image(repeats=128)
        capacity = run(checkpoint, terminal, probes)
        assert capacity.returncode == 0 and 'observations=128/128 result=PASS' in capacity.stdout
        overflow = run(checkpoint, terminal, probes + probes[:1])
        assert overflow.returncode == 2
        for fields in [('0x100000000', '0', '0'), ('-1', '0', '0'),
                       ('bad', '0', '0'), ('0', '16', '0'), ('0', '-1', '0'),
                       ('0', '0', '0x100000000'), ('0', '0', '-1')]:
            invalid = run(checkpoint, terminal, extra=['--expect-register-at', *fields])
            assert invalid.returncode == 2, fields
        for fields in [[], ['0'], ['0', '0']]:
            invalid = run(checkpoint, terminal, extra=['--expect-register-at', *fields])
            assert invalid.returncode == 2, fields
    print('PASS ARM/Thumb comparisons, skipped failure writes, order/missing/extra visits, exact capacity and input bounds')


if __name__ == '__main__':
    main()
