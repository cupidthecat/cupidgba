#!/usr/bin/env python3
"""Run an unchanged serial or HALT cartridge probe."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
from pathlib import Path
import re
import subprocess


CASES = {
    "serial_time": ("bae7c94d818df95caf5f2443dc4a2ca15c0868a52d5f89e04b75c875c9cf9af0", 0x08000374),
    "serial_time_2": ("c01fd6741759dbde664c7b32625dccd26374169a1b27f5db057488ffa4652a12", 0x08000374),
    "serial_time_3": ("7ce1dd69ff4fb9aee0f8da27c03c71ad210a42825d87e6303340e600718e3791", 0x08000378),
    "serial_time_2M": ("d850b08132f35bb4bd86b756604a0d309df2619a1d78646cd87f28b8bf0e3138", 0x08000374),
    "serial_time_2M_2": ("7705764f85b2a733284bc8dbab92f597e7ab0f6faccd87ee1d5766f72400ecd7", 0x08000374),
    "serial_time_2M_3": ("47d1e8aa4c655e751ecfdacedd068a8d33821447a5487f8e59ec3746d40101e4", 0x08000378),
    "serial_read_data": ("fde0c9fbcac6ecf35c586e090f53803094c8db6d300b69ccb602225635341c41", 0x08000368),
    "serial_time_start_bit": ("c9cdb4c11f58f1330935af90d1cd3324976358652abc82e89f0d2786ec73a46a", 0x08000340),
    "serial_time_start_bit_2": ("b57c9ef0edc59df8a610f1582ca446dbfba2cd6bd4e1e9e9dc1488f0c49e07c3", 0x08000340),
    "halt_pc": ("19ff7e328c0cf95fe663a2eb2eb6f9eae50e07a65da36ac74ec6c2c665a3b2a0", 0x08000364),
    "halt_pc_2": ("eabb9f9505601ea670289ac3a458cf94e0e0a78e8475b5f8a5fcfc997628ef95", 0x08000364),
    "halt_pc_3": ("7493755c4506af294e552732862d05557c8b1280e21daa090b296f665550b797", 0x08000364),
    "halt_pc_4": ("3d91c45881745a508c8c48aa789b5628ad82139f4471a2764973dd3d1dab6d04", 0x08000364),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--bios", type=Path)
    parser.add_argument("--test", choices=CASES, required=True)
    args = parser.parse_args()
    if args.test.startswith("halt_pc") and not args.bios:
        raise SystemExit("HALT return-address probes require --bios for the original internal routine addresses")
    digest, terminal = CASES[args.test]
    rom = args.directory / (args.test + ".gba")
    if hashlib.sha256(rom.read_bytes()).hexdigest() != digest:
        raise SystemExit("Serial probe checksum mismatch")
    command = [str(args.runner.resolve()), str(rom.resolve()), "--frames", "360",
               "--steps", "150000000", "--expect-loop-register", "12",
               "--terminal-address", hex(terminal)]
    if args.bios:
        command.extend(["--bios", str(args.bios.resolve())])
    result = subprocess.run(command, capture_output=True, text=True, timeout=90)
    print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="")
    if result.returncode or not re.search(r"^terminal=yes r12=0 result=PASS$", result.stdout, re.MULTILINE):
        raise SystemExit("Serial probe did not reach its passing terminal result")
    print(f"PASS unchanged serial probe: {args.test}")


if __name__ == "__main__":
    main()
