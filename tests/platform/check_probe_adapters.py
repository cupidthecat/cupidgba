#!/usr/bin/env python3
"""Exercise each production probe's completion gate with owned DMA-held terminals."""

# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
from pathlib import Path
import re
import struct
import subprocess
import tempfile

BASE = 0x08000000
DMA = [
    0xE3A00301,  # MOV r0, #IO
    0xE3A01403,  # MOV r1, #IWRAM
    0xE58010D4,  # STR r1, DMA3SAD
    0xE3A01402,  # MOV r1, #EWRAM
    0xE58010D8,  # STR r1, DMA3DAD
    0xE3A01000,  # MOV r1, #0 (maximum transfer count)
    0xE1C01DBC,  # STRH r1, DMA3CNT_L
    0xE3A01C85,  # MOV r1, #0x8500 (fixed-source words)
    0xE1C01DBE,  # STRH r1, DMA3CNT_H
    0xE3A00000,  # MOV r0, #0; the following branch waits for DMA.
    0xEAFFFFFE,  # B .
]


def branch(source, target, condition=0xEA):
    assert not (source | target) & 3
    displacement = (target - source - 8) // 4
    assert -(1 << 23) <= displacement < (1 << 23)
    return condition << 24 | displacement & 0xFFFFFF


def simple_cartridge(kind):
    words = [0xE3A0C000, 0xE3A03009]  # Zero result, nonzero observed operand.
    if kind == "user_bank":
        words += [0xE3A02000, 0xE321F0D1]  # Shared r2=0, enter masked FIQ mode.
    operand = BASE + 0xC0 + len(words) * 4
    words += [0xE1A00000, 0xE3530009]  # Observed NOP; CMP r3, #9.
    guard = BASE + 0xC0 + len(words) * 4
    words += [0x1A000000]  # BNE is observed but its false predicate falls through.
    checkpoint = BASE + 0xC0 + len(words) * 4
    words += [0xE1A00002 if kind == "user_bank" else 0xE92D0003]
    words += DMA
    terminal = BASE + 0xC0 + (len(words) - 1) * 4
    data = struct.pack("<I", branch(0, 0xC0)) + bytes(0xBC)
    data += struct.pack("<" + "I" * len(words), *words)
    return data, (operand, guard, checkpoint, terminal)


def corpus_cartridge(bad=False):
    # This owned corpus tests transport and completion, not arithmetic accuracy.
    # Every one of its 10,000 records reaches the real driver's original sites.
    data = bytearray(203708)

    def put(offset, *words):
        struct.pack_into("<" + "I" * len(words), data, offset, *words)

    put(0, branch(0, 0x100))
    put(0xC8, *DMA)  # The driver's fixed terminal remains at 0xF0.
    put(0x100, 0xE59FB008, 0xE59FC008, branch(0x108, 0x31238), 0xE1A00000)
    put(0x110, BASE + 0x4D0, 10000)
    put(0x4CC, 10000)
    for index in range(10000):
        put(0x4D0 + 20 * index, index, index ^ 0xFFFFFFFF, 0x13579BDF, 0x2468ACE0, 0)
    put(
        0x31238,
        0xE8BB00E3,  # LDMIA r11!, {r0,r1,r5,r6,r7}: checked input record.
        0xE3A0A0FF,  # Checked loaded-record site.
        0xE1A0800B,  # Save the next table pointer.
        0xE3A03000 if bad else 0xE1A03005,  # Controlled wrong result, or owned transport.
        0xE1A04006,
        0xE3A07000,
        0xE1A0B007,
        branch(0x31254, 0x316A4),
    )
    put(
        0x316A4,
        0xE1530005,
        0x1A000004,
        0xE1540006,
        0x1A000002,
        0xE15B0007,
        0x1A000000,
        0xE1A0B008,  # Restore the next table pointer after all three comparisons.
        0xE25CC001,  # SUBS r12, r12, #1.
        branch(0x316C4, 0x31238, 0x1A),
        branch(0x316C8, 0xC8),
    )
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("irq", "status", "user_bank", "corpus"):
        parser.add_argument("--" + name.replace("_", "-"), type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cupidgba-probe-adapters-") as folder:
        folder = Path(folder)
        for kind in ("irq", "status", "user_bank", "corpus"):
            runner = getattr(args, kind).resolve()
            rom = folder / (kind + ".gba")
            if kind == "corpus":
                rom.write_bytes(corpus_cartridge())
                tail = [str(rom), str(folder / "result.bin")]
                bad_tail = tail
            else:
                image, (operand, guard, checkpoint, terminal) = simple_cartridge(kind)
                rom.write_bytes(image)
                if kind == "status":
                    tail = [
                        str(rom),
                        "-",
                        hex(checkpoint),
                        hex(terminal),
                        hex(operand),
                        "0xe1a00000",
                        "0x1f",
                        "3",
                        "9",
                    ]
                elif kind == "irq":
                    tail = [
                        "probe",
                        str(rom),
                        "-",
                        hex(checkpoint),
                        hex(terminal),
                        hex(guard),
                        "0",
                        "0x1a000000",
                        "1",
                    ]
                else:
                    tail = [
                        "probe",
                        str(rom),
                        "-",
                        hex(checkpoint),
                        "2",
                        hex(terminal),
                        hex(guard),
                        "0x1a000000",
                        "1",
                    ]
                bad_tail = tail[:-1] + ["8" if kind == "status" else "0"]

            def run(limit, command_tail):
                return subprocess.run(
                    [str(runner), str(limit), *command_tail],
                    capture_output=True,
                    text=True,
                    timeout=10,
                )

            passing = run(64, tail)
            assert passing.returncode == 0 and "result=PASS" in passing.stdout, (
                passing.stdout + passing.stderr
            )
            if kind == "corpus":
                assert (
                    "cases=10000/10000 loads=10000 comparisons=30000/30000 branches=30000/30000"
                    in passing.stdout
                )
                match = re.search(r"\bframes=(\d+)\b", passing.stdout)
                assert match is not None
                deadline = int(match[1])
                assert 2 <= deadline < 64
            else:
                deadline = 2
            late = run(deadline, tail)
            assert late.returncode == 1 and "result=FAIL" in late.stdout, late.stdout + late.stderr
            assert "owned-boundary issued=1 completed=0" in late.stdout, late.stdout
            if kind == "corpus":
                assert (
                    "cases=10000/10000 loads=10000 comparisons=30000/30000 branches=30000/30000"
                    in late.stdout
                )
                rom.write_bytes(corpus_cartridge(bad=True))
            invalid = run(64, bad_tail)
            assert invalid.returncode == 1 and "result=FAIL" in invalid.stdout, (
                invalid.stdout + invalid.stderr
            )
            print(f"PASS {kind}: complete terminal, expired deadline and wrong-result controls")


if __name__ == "__main__":
    main()
