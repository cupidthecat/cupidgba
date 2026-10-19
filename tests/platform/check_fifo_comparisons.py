#!/usr/bin/env python3
"""Exercise pinned FIFO comparison qualification with an owned cartridge."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    runner = parser.parse_args().runner.resolve()
    checker = Path(__file__).parents[1] / 'roms/check_fifo_probe.py'
    with tempfile.TemporaryDirectory(prefix='cupidgba-fifo-comparisons-') as directory:
        folder = Path(directory)
        local_checker = folder / checker.name
        local_checker.write_bytes(checker.read_bytes())
        rom = folder / 'owned.gba'
        data = bytearray(256)
        # Both outcomes bypass the failure write. R12=0 alone cannot qualify
        # the CMP at 0Ch. A second CMP at 18h is deliberately never issued.
        struct.pack_into('<7I', data, 0, 0xE3A0C000, 0xE3A0004C, 0xE59F1024,
                         0xE1500001, 0xEA00001A, 0xE3A0C001, 0xE1500001)
        struct.pack_into('<I', data, 0x34, 0x4C)
        struct.pack_into('<5I', data, 0x80, 0xE92D0003, 0xE3A00301, 0xE5901004, 0xE3110001, 0xEAFFFFFE)
        comparison = dict(address='0x0800000c', mode='arm', instruction='0xe1500001',
                          register=0, value='0x0000004c', literal='0x08000034')

        def run(observations=None):
            rom.write_bytes(data)
            case = dict(name='owned', path=rom.name, sha256=hashlib.sha256(data).hexdigest(),
                        checkpoint='0x08000080', terminal='0x08000090',
                        observations=[comparison] if observations is None else observations)
            (folder / 'fifo-probes.json').write_text(json.dumps(dict(cases=[case])), encoding='utf-8')
            return subprocess.run([sys.executable, str(local_checker), '--directory', str(folder),
                                   '--runner', str(runner), '--test', 'owned'],
                                  text=True, capture_output=True, timeout=3)

        passed = run()
        assert passed.returncode == 0 and 'observations=1/1 result=PASS' in passed.stdout, passed.stdout + passed.stderr
        # Preserve the original zero-result blind spot as a negative control.
        struct.pack_into('<I', data, 4, 0xE3A00044)
        legacy = run([])
        assert legacy.returncode == 0 and 'terminal=yes r12=0 result=PASS' in legacy.stdout
        rejected = run()
        assert rejected.returncode == 1 and 'r0=00000044 expected=0000004c r1=0000004c expected=0000004c result=FAIL' in rejected.stdout
        assert 'checkpoint=yes r12=0 result=PASS' in rejected.stdout
        assert 'terminal=yes r12=0 result=PASS' in rejected.stdout
        struct.pack_into('<I', data, 4, 0xE3A0004C)
        missing = run([comparison, dict(comparison, address='0x08000018')])
        assert missing.returncode == 1 and 'observations=1/2 result=MISSING' in missing.stdout
        # A matching word elsewhere in the image does not establish the live
        # right-hand operand. Require R1 as well as the measured R0 at issue.
        struct.pack_into('<I', data, 0x34, 0x4D)
        struct.pack_into('<I', data, 0x38, 0x4C)
        wrong_expected = run([dict(comparison, literal='0x08000038')])
        assert wrong_expected.returncode == 1 and 'r1=0000004d expected=0000004c result=FAIL' in wrong_expected.stdout
        assert 'terminal=yes r12=0 result=PASS' in wrong_expected.stdout
        struct.pack_into('<I', data, 0x34, 0x4C)
        for change, diagnostic in [
            ({'instruction': '0xe1510000'}, 'pinned instruction'),
            ({'register': 1}, 'original CMP operand'),
            ({'value': '0x4d'}, 'original expected literal'),
            ({'literal': '0x100000000'}, 'original expected literal'),
        ]:
            invalid = run([dict(comparison, **change)])
            assert invalid.returncode == 1 and diagnostic in invalid.stderr, invalid.stdout + invalid.stderr
        struct.pack_into('<I', data, 0xC, 0xE350004C)
        immediate = dict(comparison, instruction='0xe350004c')
        assert run([immediate]).returncode == 0
        invalid = run([dict(immediate, value='0x4d')])
        assert invalid.returncode == 1 and 'encoded immediate' in invalid.stderr
    print('PASS original literals/immediates, CMP operands, bypassed failure writes and missing issued comparisons')


if __name__ == '__main__':
    main()
