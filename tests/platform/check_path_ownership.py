#!/usr/bin/env python3
# @file check_path_ownership.py
# @brief Preserve owned inputs and outputs across runner and frontend path aliases.
"""Check file ownership with generated cartridges, batteries and capture paths."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import itertools
import os
from pathlib import Path
import struct
import stat
import subprocess
import tempfile

from check_runner_rtc import battery


OUTPUTS = ("--screenshot", "--dump-vram", "--dump-io", "--dump-ewram",
           "--dump-save", "--dump-rtc", "--dump-audio", "--dump-rumble")


class Files:
    def __init__(self, folder, rom_name="owned.gba", rom_size=512):
        self.folder = folder
        self.inputs = folder / "inputs"
        self.inputs.mkdir(parents=True)
        self.rom = self.inputs / rom_name
        image = bytearray(rom_size)
        struct.pack_into("<I", image, 0, 0xEAFFFFFE)
        image[0xA0:0xAC] = b"OWNED PATH  "
        self.rom.write_bytes(image)
        self.bios = self.inputs / "firmware.bin"
        self.bios.write_bytes(struct.pack("<I", 0xEAFFFFFE) * 4096)
        self.script = self.inputs / "keys.txt"
        self.script.write_text("0 0\n", encoding="ascii")
        self.card = self.inputs / "card.bin"
        self.card.write_bytes(bytes([255]) * 3520)
        self.save = self.inputs / "battery.bin"
        self.save.write_bytes(bytes([0xA5]) * 32768)
        self.rtc = self.inputs / "clock.bin"
        self.rtc.write_bytes(battery(image, bytes([0, 1, 1, 6, 0, 0, 0])))
        self.watched = [self.rom, self.bios, self.script, self.card, self.save, self.rtc]

    def source(self, kind):
        return {
            "rom": (self.rom, []),
            "bios": (self.bios, ["--bios", str(self.bios)]),
            "script": (self.script, ["--key-script", str(self.script)]),
            "card": (self.card, ["--card-reader", "on", "--card", str(self.card)]),
            "save": (self.save, ["--load-save", str(self.save)]),
            "rtc": (self.rtc, ["--load-rtc", str(self.rtc)]),
        }[kind]

    def command(self, runner):
        return [str(runner), str(self.rom), "--frames", "1", "--steps", "1000000",
                "--start-address", "0x08000000", "--save-type", "sram", "--rtc", "on"]

    def reject(self, command, extra=()):
        watched = self.watched + list(extra)
        before = {p: p.read_bytes() for p in watched}
        result = subprocess.run(command, capture_output=True, timeout=15, env=ENV)
        assert result.returncode != 0, result.stdout + result.stderr
        assert any(text in result.stderr for text in
                   (b"Output path overlaps", b"Cannot use output file", b"Cannot verify file paths")), result.stderr
        assert b"frames=1 " not in result.stdout, result.stdout
        for path, data in before.items():
            assert path.read_bytes() == data, f"Rejected command changed {path}"


def directory_link(link, target):
    if os.name == "nt":
        import _winapi
        _winapi.CreateJunction(str(target), str(link))
    else:
        link.symlink_to(target, target_is_directory=True)


def literal_inputs(files, runner):
    cases = 0
    for kind in ("rom", "bios", "script", "card", "save", "rtc"):
        source, options = files.source(kind)
        for output in OUTPUTS:
            # In-place battery and RTC writeback are tested as positive controls.
            if (kind, output) in (("save", "--dump-save"), ("rtc", "--dump-rtc")):
                continue
            files.reject(files.command(runner) + options + [output, str(source)])
            cases += 1
    return cases


def linked_inputs(files, runner):
    link = files.folder / "directory alias"
    directory_link(link, files.inputs)
    cases = 0
    for kind in ("rom", "bios", "script", "card", "save", "rtc"):
        source, options = files.source(kind)
        hard = files.folder / (kind + "-hard.bin")
        os.link(source, hard)
        aliases = [hard, link / source.name]
        if os.name != "nt":
            symbolic = files.folder / (kind + "-symbolic.bin")
            symbolic.symlink_to(source)
            aliases.append(symbolic)
        for alias in aliases:
            for output in OUTPUTS:
                if (kind, output) in (("save", "--dump-save"), ("rtc", "--dump-rtc")):
                    continue
                files.reject(files.command(runner) + options + [output, str(alias)], [alias])
                cases += 1
    return cases


def output_pairs(files, runner):
    cases = 0
    for index, (first, second) in enumerate(itertools.combinations(OUTPUTS, 2)):
        path = files.folder / f"existing-{index}.bin"
        path.write_bytes(b"keep complete prior output")
        hard = files.folder / f"hard-{index}.bin"
        os.link(path, hard)
        missing = files.folder / f"missing-{index}.bin"
        for a, b in ((path, path), (path, hard), (missing, missing)):
            files.reject(files.command(runner) + [first, str(a), second, str(b)],
                         [p for p in (a, b) if p.exists()])
            assert not missing.exists(), "Rejected outputs created a new file"
            cases += 1
    return cases


def normalized_names(files, runner):
    child = files.folder / "child"
    child.mkdir()
    directory = files.folder / "directory alias"
    directory_link(directory, files.folder / "inputs")
    alias = directory / "future.bin"
    target = files.inputs / "future.bin"
    pairs = [(child / ".." / "new.bin", files.folder / "new.bin"), (alias, target)]
    if os.name == "nt":
        pairs.extend(((files.folder / "FUTURE.bin", files.folder / "future.bin"),
                      (files.folder / "trailing.bin.", files.folder / "trailing.bin"),
                      (files.folder / "space.bin ", files.folder / "space.bin"),
                      (files.folder / "\u00c9.bin", files.folder / "\u00e9.bin")))
        files.reject(files.command(runner) + ["--dump-audio", str(files.rom).upper()])
    for first, second in pairs:
        files.reject(files.command(runner) + ["--dump-audio", str(first), "--dump-ewram", str(second)])
        assert not first.exists() and not second.exists()
    bad = [files.inputs, files.folder / "missing-parent" / "new.bin"]
    if os.name != "nt":
        dangling = files.folder / "dangling"
        dangling.symlink_to(files.folder / "absent.bin")
        pipe = files.folder / "capture-pipe"
        os.mkfifo(pipe)
        bad.extend((dangling, pipe))
    for output in OUTPUTS:
        for target in bad:
            files.reject(files.command(runner) + [output, str(target)])
    # Backslash is an ordinary POSIX directory character, not a separator.
    if os.name != "nt":
        unusual = files.folder / "parent\\"
        unusual.mkdir()
        output = unusual / "accepted.audio"
        result = subprocess.run(files.command(runner) + ["--dump-audio", str(output)],
                                capture_output=True, text=True, timeout=15, env=ENV)
        assert result.returncode == 0 and output.stat().st_size > 0, result.stderr
        upper, lower = files.folder / "\u00c9.bin", files.folder / "\u00e9.bin"
        result = subprocess.run(files.command(runner) + ["--dump-audio", str(upper),
                                "--dump-ewram", str(lower)], capture_output=True, text=True,
                                timeout=15, env=ENV)
        assert result.returncode == 0 and not os.path.samefile(upper, lower), result.stderr
        assert upper.stat().st_size > 0 and lower.stat().st_size == 262144
    return len(pairs) + len(OUTPUTS) * len(bad) + (1 if os.name == "nt" else 2)


def writeback(files, runner):
    unique = [files.folder / f"output-{i}.bin" for i in range(len(OUTPUTS))]
    command = files.command(runner)
    for flag, path in zip(OUTPUTS, unique):
        command += [flag, str(path)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=15, env=ENV)
    assert result.returncode == 0, result.stdout + result.stderr
    assert [p.stat().st_size for p in unique[:6]] == [115215, 98304, 1024, 262144, 32768, 44]
    assert unique[6].stat().st_size > 0
    rumble = unique[7].read_bytes()
    assert len(rumble) == 32 and rumble[:8] == b"CPDRMB01"
    assert rumble[8:20] == bytes(12) and rumble[28:] == bytes(4)
    assert struct.unpack_from("<Q", rumble, 20)[0] > 0
    save_before = files.save.read_bytes()
    for _ in range(2):
        result = subprocess.run(files.command(runner) + ["--load-save", str(files.save),
                                "--dump-save", str(files.save), "--load-rtc", str(files.rtc),
                                "--dump-rtc", str(files.rtc)], capture_output=True, text=True,
                                timeout=15, env=ENV)
        assert result.returncode == 0, result.stdout + result.stderr
        assert files.save.read_bytes() == save_before
        assert len(files.rtc.read_bytes()) == 44
    # Two input roles may refer to one physical file.
    shared = files.folder / "shared.gba"
    shared_image = bytearray(files.bios.read_bytes())
    shared_image[0xA0:0xB0] = files.rom.read_bytes()[0xA0:0xB0]
    shared.write_bytes(shared_image)
    result = subprocess.run([str(runner), str(shared), "--bios", str(shared), "--frames", "1",
                            "--steps", "1000000", "--start-address", "0x08000000"],
                            capture_output=True, text=True, timeout=15, env=ENV)
    assert result.returncode == 0, result.stdout + result.stderr
    before = files.save.read_bytes()
    mode = stat.S_IMODE(files.save.stat().st_mode)
    files.save.chmod(stat.S_IREAD)
    try:
        result = subprocess.run(files.command(runner) + ["--load-save", str(files.save),
                                "--dump-save", str(files.save)], capture_output=True, text=True,
                                timeout=15, env=ENV)
        assert result.returncode != 0 and files.save.read_bytes() == before, result.stderr
    finally:
        files.save.chmod(mode)
    if os.name != "nt":
        import resource
        import signal

        def limit_output():
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            resource.setrlimit(resource.RLIMIT_FSIZE, (4096, 4096))

        result = subprocess.run(files.command(runner) + ["--load-save", str(files.save),
                                "--dump-save", str(files.save)], capture_output=True, text=True,
                                timeout=15, env=ENV, preexec_fn=limit_output)
        assert result.returncode != 0 and files.save.read_bytes() == before, result.stderr
    return 5 if os.name == "nt" else 6


def frontend_cases(folder, frontend, linked):
    cases = 0
    for count in ((2, 3, 4) if linked else (1,)):
        for role in ("rom", "bios", "script", "card"):
            for extension in ("sav", "rtc"):
                files = Files(folder / f"{count}-{role}-{extension}")
                if linked:
                    peers = []
                    for i in range(1, count):
                        peer = files.inputs / f"peer-{i}.gba"
                        peer.write_bytes(files.rom.read_bytes())
                        files.watched.append(peer)
                        peers += ["--link", str(peer)]
                    destination = Path(peers[-1]).with_suffix(f".p{count}.{extension}")
                else:
                    peers = []
                    destination = files.rom.with_suffix("." + extension)
                source, options = files.source(role)
                if role == "bios":
                    options = ["-b", str(source)]
                os.link(source, destination)
                command = [str(frontend), str(files.rom), *peers, "--frames", "1",
                           "-S", "sram", "--rtc", "on", *options]
                files.reject(command, [destination])
                cases += 1
        # Repeated read-only ROMs retain independent derived battery ownership.
        files = Files(folder / f"positive-{count}")
        image = bytearray(files.rom.read_bytes())
        # Write one SRAM byte, making each endpoint's independent battery dirty.
        struct.pack_into("<4I", image, 0, 0xE3A0040E, 0xE3A0105A, 0xE5C01000, 0xEAFFFFFE)
        files.rom.write_bytes(image)
        command = [str(frontend), str(files.rom), "--frames", "1", "-S", "sram", "--rtc", "off"]
        for _ in range(1, count):
            command += ["--link", str(files.rom)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=15, env=ENV)
        assert result.returncode == 0, result.stdout + result.stderr
        expected = [files.rom.with_suffix(f".p{i + 1}.sav") for i in range(count)] if linked else [files.rom.with_suffix(".sav")]
        assert all(p.stat().st_size == 32768 for p in expected)
        assert all(p.read_bytes()[0] == 0x5A for p in expected)
        assert files.rom.read_bytes() == image
        cases += 1
        # Disabled or absent persistence hardware does not claim a sidecar path.
        for extension in ("sav", "rtc"):
            disabled = Files(folder / f"disabled-{count}-{extension}", f"disabled.{extension}")
            before = {p: p.read_bytes() for p in disabled.watched}
            command = [str(frontend), str(disabled.rom), "--frames", "1", "-S", "none"]
            for _ in range(1, count):
                command += ["--link", str(disabled.rom)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=15, env=ENV)
            assert result.returncode == 0, result.stdout + result.stderr
            assert all(p.read_bytes() == data for p, data in before.items())
            cases += 1
    return cases


ENV = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--frontend", type=Path)
    parser.add_argument("--group", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cupidgba-path-ownership-") as temporary:
        folder = Path(temporary)
        if args.group.startswith("frontend-"):
            assert args.frontend
            cases = frontend_cases(folder, args.frontend.resolve(), args.group.endswith("local"))
        else:
            operations = {"inputs": literal_inputs, "links": linked_inputs,
                          "outputs": output_pairs, "names": normalized_names, "writeback": writeback}
            cases = operations[args.group](Files(folder), args.runner.resolve())
    print(f"PASS path ownership {args.group}: {cases} cases")


if __name__ == "__main__":
    main()
