#!/usr/bin/env python3
"""Check deterministic RTC inputs, guest calendar reads and explicit battery snapshots."""

# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
from datetime import datetime, timedelta
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "roms"))
from rtc_input_driver import cartridge, stopped_cartridge

CLOCK_HZ = 16777216


def fnv(data, bits):
    value, prime = (14695981039346656037, 1099511628211) if bits == 64 else (2166136261, 16777619)
    for byte in data:
        value = ((value ^ byte) * prime) & ((1 << bits) - 1)
    return value


def calendar(seed):
    date = datetime.strptime(seed, "%Y-%m-%dT%H:%M:%S")
    return bytes(
        [
            date.year - 2000,
            date.month,
            date.day,
            (date.weekday() + 1) % 7,
            date.hour,
            date.minute,
            date.second,
        ]
    )


def wire(date, control=0x40, invalid_second=False):
    data = bytearray(value // 10 * 16 + value % 10 for value in date)
    if not control & 0x40:
        hour = date[4] % 12
        data[4] = hour // 10 * 16 + hour % 10
    if invalid_second:
        data[6] = date[6]
    # The hour supplies the PM read flag in 24-hour mode as well.
    if date[4] >= 12:
        data[4] |= 0x80
    return data


def battery(
    image, date, fraction=0, parameters=0, stamp=0, carry=False, control=0x40, invalid_second=False
):
    data = bytearray(44)
    data[:8] = b"CPDRTC03" if carry else b"CPDRTC02" if parameters else b"CPDRTC01"
    struct.pack_into("<Q", data, 8, fnv(image, 64))
    data[16:23] = date
    data[23] = control
    struct.pack_into("<I", data, 24, fraction)
    data[28] = 2 | (8 if carry else 0) | int(invalid_second)
    struct.pack_into("<H", data, 29, parameters)
    struct.pack_into("<Q", data, 32, stamp)
    struct.pack_into("<I", data, 40, fnv(data[:40], 32))
    return data


def checksum(data):
    struct.pack_into("<I", data, 40, fnv(data[:40], 32))
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--bios", type=Path)
    args = parser.parse_args()
    count = 0
    with tempfile.TemporaryDirectory(prefix="cupid-rtc-input-") as directory:
        root = Path(directory)
        rom = root / "owned.gba"

        def run(
            image,
            observation,
            extra=(),
            expect=0,
            expected_date=None,
            fraction=0,
            parameters=0,
            snapshot=True,
            carry=False,
            control=0x40,
            invalid_second=False,
        ):
            nonlocal count
            count += 1
            rom.write_bytes(image)
            prefix = root / str(count)
            ram = prefix.with_suffix(".ram")
            state = prefix.with_suffix(".rtc")
            io = prefix.with_suffix(".io")
            command = [
                str(args.runner.resolve()),
                str(rom),
                "--start-address",
                "0x08000000",
                "--frames",
                "1",
                "--steps",
                "100000",
                "--terminal-address",
                "0x08000110",
                "--expect-loop-register",
                "12",
                "--expect-register-pair-at",
                hex(observation),
                "7",
                "7",
                "5",
                "0x02000007",
                "--dump-ewram",
                str(ram),
                "--dump-io",
                str(io),
            ]
            if snapshot:
                command += ["--dump-rtc", str(state)]
            if args.bios:
                command += ["--bios", str(args.bios.resolve())]
            result = subprocess.run(
                command + list(extra), capture_output=True, text=True, timeout=3
            )
            if result.returncode != expect:
                raise SystemExit(
                    f"RTC run {count}: exit {result.returncode}, expected {expect}\n"
                    f"{result.stdout}{result.stderr}"
                )
            if rom.read_bytes() != image:
                raise SystemExit("RTC replay changed its cartridge")
            if expect:
                if ram.exists() or io.exists() or state.exists():
                    raise SystemExit("Rejected RTC startup created an output")
                return result, None, None
            if "terminal-issued=yes completed=yes pc=08000110" not in result.stdout.splitlines():
                raise SystemExit("RTC reader did not complete its original terminal")
            if "observations=1/1 result=PASS" not in result.stdout.splitlines():
                raise SystemExit("RTC reader did not complete all seven guest transfers")
            expected_ram = bytearray(0x40000)
            expected_ram[:7] = (
                wire(expected_date, control, invalid_second)
                if expected_date is not None
                else bytes(7)
            )
            struct.pack_into("<III", expected_ram, 16, 0x52494331, 7, 0x02000007)
            if ram.read_bytes() != expected_ram:
                raise SystemExit(
                    f"RTC run {count} guest bytes differ: {ram.read_bytes()[:28].hex()}"
                )
            match = re.search(r"^frames=0 cycles=(\d+) steps=\d+ pc=08000114$", result.stdout, re.M)
            if not match:
                raise SystemExit("RTC reader endpoint is incomplete")
            clocks = int(match.group(1))
            if snapshot:
                if fraction + clocks >= CLOCK_HZ:
                    raise SystemExit("Owned short RTC trial exceeded its calendar interval")
                expected_state = battery(
                    image,
                    expected_date,
                    fraction + clocks,
                    parameters,
                    carry=carry,
                    control=control,
                    invalid_second=invalid_second,
                )
                if state.read_bytes() != expected_state:
                    raise SystemExit(
                        f"RTC run {count} complete battery differs\n"
                        f"actual {state.read_bytes().hex()}\nexpected {expected_state.hex()}"
                    )
            return result, state, clocks

        seeds = [
            "2000-01-01T00:00:00",
            "2004-02-29T12:34:56",
            "2024-02-28T23:59:59",
            "2026-11-01T04:05:06",
            "2099-12-31T23:59:59",
        ]
        for thumb in (False, True):
            image, observation = cartridge(thumb)
            for seed in seeds:
                run(image, observation, ["--rtc-time", seed], expected_date=calendar(seed))
            default = calendar(seeds[0])
            _, state, clocks = run(image, observation, expected_date=default)
            original = state.read_bytes()
            _, restored, next_clocks = run(
                image,
                observation,
                ["--load-rtc", str(state)],
                expected_date=default,
                fraction=clocks,
            )
            if state.read_bytes() != original or restored.read_bytes()[32:40] != bytes(8):
                raise SystemExit("RTC input changed or retained an offline host anchor")
            if int.from_bytes(restored.read_bytes()[24:28], "little") != clocks + next_clocks:
                raise SystemExit("RTC fractional clocks did not survive the second process")
            # A loaded host timestamp never changes the deterministic calendar.
            supplied = root / "supplied.rtc"
            source = battery(image, calendar(seeds[1]), 123456, 0x1234, (1 << 63) + 123)
            supplied.write_bytes(source)
            run(
                image,
                observation,
                ["--load-rtc", str(supplied)],
                expected_date=calendar(seeds[1]),
                fraction=123456,
                parameters=0x1234,
            )
            run(
                image,
                observation,
                ["--rtc-time", seeds[3], "--load-rtc", str(supplied)],
                expected_date=calendar(seeds[3]),
                parameters=0x1234,
            )
            if supplied.read_bytes() != source:
                raise SystemExit("Loading or overriding a calendar changed its input file")
            # Version 3 retains a live minute carry; an explicit seed clears it.
            carry_path = root / "carry.rtc"
            carry_date = calendar("2024-03-01T12:00:00")
            carry_source = battery(image, carry_date, 25, 0x1234, carry=True)
            carry_path.write_bytes(carry_source)
            run(
                image,
                observation,
                ["--load-rtc", str(carry_path)],
                expected_date=carry_date,
                fraction=25,
                parameters=0x1234,
                carry=True,
            )
            run(
                image,
                observation,
                ["--load-rtc", str(carry_path), "--rtc-time", seeds[3]],
                expected_date=calendar(seeds[3]),
                parameters=0x1234,
            )
            if carry_path.read_bytes() != carry_source:
                raise SystemExit("Loading a carry snapshot changed its input file")
            # Calendar seeds preserve a loaded hour format and clear invalid seconds.
            special = root / "special.rtc"
            noon = calendar(seeds[1])
            special.write_bytes(battery(image, noon, 123, 0x1234, control=0))
            run(
                image,
                observation,
                ["--load-rtc", str(special)],
                expected_date=noon,
                fraction=123,
                parameters=0x1234,
                control=0,
            )
            run(
                image,
                observation,
                ["--load-rtc", str(special), "--rtc-time", seeds[4]],
                expected_date=calendar(seeds[4]),
                parameters=0x1234,
                control=0,
            )
            invalid_date = bytearray(noon)
            invalid_date[6] = 0x70
            special.write_bytes(battery(image, invalid_date, 123, 0x1234, invalid_second=True))
            invalid_source = special.read_bytes()
            run(
                image,
                observation,
                ["--load-rtc", str(special)],
                expected_date=invalid_date,
                fraction=123,
                parameters=0x1234,
                invalid_second=True,
            )
            run(
                image,
                observation,
                ["--load-rtc", str(special), "--rtc-time", seeds[3]],
                expected_date=calendar(seeds[3]),
                parameters=0x1234,
            )
            if special.read_bytes() != invalid_source:
                raise SystemExit("Calendar repair changed the input battery")
            run(image, observation, ["--rtc", "off"], expected_date=None, snapshot=False)
            absent, absent_observation = cartridge(thumb, False)
            run(absent, absent_observation, expected_date=None, snapshot=False)
            run(
                absent,
                absent_observation,
                ["--rtc", "on", "--rtc-time", seeds[2]],
                expected_date=calendar(seeds[2]),
            )

        for thumb in (False, True):
            count += 1
            image = stopped_cartridge(thumb)
            rom.write_bytes(image)
            state = root / ("stop-thumb.rtc" if thumb else "stop-arm.rtc")
            seed = seeds[3]
            command = [
                str(args.runner.resolve()),
                str(rom),
                "--start-address",
                "0x08000000",
                "--frames",
                "2",
                "--steps",
                "100000",
                "--rtc-time",
                seed,
                "--dump-rtc",
                str(state),
            ]
            if args.bios:
                command += ["--bios", str(args.bios.resolve())]
            result = subprocess.run(command, capture_output=True, text=True, timeout=3)
            running = re.search(r"^frames=0 cycles=(\d+) steps=\d+ pc=", result.stdout, re.M)
            stopped = re.search(r"^stopped_cycles=(\d+)$", result.stdout, re.M)
            if result.returncode != 1 or not running or not stopped:
                raise SystemExit(
                    "Owned STOP trial did not retain its expected frame deadline: "
                    + result.stdout
                    + result.stderr
                )
            elapsed = int(running.group(1)) + int(stopped.group(1))
            # Preserve the runner's existing 300-frame startup allowance.
            advanced = datetime.fromisoformat(seed) + timedelta(seconds=elapsed // CLOCK_HZ)
            expected = battery(image, calendar(advanced.isoformat()), elapsed % CLOCK_HZ)
            if elapsed != 302 * 280896 or state.read_bytes() != expected:
                raise SystemExit("RTC snapshot lost elapsed STOP battery clocks")
            if rom.read_bytes() != image:
                raise SystemExit("STOP snapshot changed its cartridge")

        image, observation = cartridge(False)
        for extra in [
            ["--rtc", "off"],
            ["--rtc", "invalid"],
            ["--rtc-time", "2023-02-29T00:00:00"],
            ["--rtc-time", "1999-12-31T23:59:59"],
            ["--rtc-time", "2100-01-01T00:00:00"],
            ["--rtc-time", "2026-11-01T24:00:00"],
            ["--rtc-time", "2026-11-01"],
            ["--rtc-time"],
            ["--rtc"],
            ["--load-rtc"],
            ["--dump-rtc"],
            ["--load-rtc", str(root / "missing.rtc")],
        ]:
            run(image, observation, extra, expect=2)
        absent, address = cartridge(False, False)
        run(absent, address, expect=2)
        run(absent, address, ["--rtc-time", seeds[0]], expect=2)
        valid = battery(image, calendar(seeds[0]), 123)
        invalid = [valid[:-1], valid + b"\0"]
        for offset, value in [(0, ord("X")), (8, valid[8] ^ 1), (17, 0), (23, 1), (31, 1)]:
            changed = bytearray(valid)
            changed[offset] = value
            invalid.append(checksum(changed))
        changed = bytearray(valid)
        changed[40] ^= 1
        invalid.append(changed)
        source = root / "invalid.rtc"
        for data in invalid:
            source.write_bytes(data)
            run(image, observation, ["--load-rtc", str(source)], expect=2)
            if source.read_bytes() != data:
                raise SystemExit("Rejected RTC battery was modified")
        # Validate startup before opening any requested stream or replacing a file.
        outputs = [
            root / name for name in ("existing.ppm", "existing.pcm", "existing.rmb", "existing.rtc")
        ]
        for path in outputs:
            path.write_bytes(b"preserve existing output")
        extra = ["--load-rtc", str(source)]
        for option, path in zip(
            ("--screenshot", "--dump-audio", "--dump-rumble", "--dump-rtc"), outputs
        ):
            extra += [option, str(path)]
        run(image, observation, extra, expect=2, snapshot=False)
        if any(path.read_bytes() != b"preserve existing output" for path in outputs):
            raise SystemExit("Invalid RTC input changed an existing output")
        # Atomic replacement must not turn a failed destination into a passing run.
        blocked = root / "blocked"
        blocked.mkdir()
        sentinel = blocked / "keep"
        sentinel.write_bytes(b"keep")
        rom.write_bytes(image)
        command = [
            str(args.runner.resolve()),
            str(rom),
            "--start-address",
            "0x08000000",
            "--frames",
            "1",
            "--steps",
            "100000",
            "--expect-loop-register",
            "12",
            "--terminal-address",
            "0x08000110",
            "--dump-rtc",
            str(blocked),
        ]
        if args.bios:
            command += ["--bios", str(args.bios.resolve())]
        before = set(root.iterdir())
        result = subprocess.run(command, capture_output=True, text=True, timeout=3)
        if (
            result.returncode != 2
            or sentinel.read_bytes() != b"keep"
            or set(root.iterdir()) != before
        ):
            raise SystemExit("RTC snapshot failure was not reported safely")
    print(
        f"PASS {count + 1} RTC startup/replay trials: guest bytes, complete batteries and failure ownership"
    )


if __name__ == "__main__":
    main()
