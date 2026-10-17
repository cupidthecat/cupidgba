# CupidGBA

CupidGBA is a Game Boy Advance emulator written in C11 with SDL2 for video,
audio, and input. Hardware accuracy and compatibility are under active work.

## Build and run

On Debian or Ubuntu, install `build-essential`, `cmake`, `pkg-config`, and
`libsdl2-dev`, then run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
./build/cupidgba game.gba -b /path/to/gba-bios.bin
```

Local play: `cupidgba first.gba --link second.gba`. See
[local sessions](docs/hardware/serial/local-session.md) for controls and saves.

The Linux Makefile also supports `make`. The BIOS argument is optional;
without it, the emulator uses a built-in replacement BIOS and HLE calls.
See [build instructions](docs/development/building.md) for Windows and core
builds, and [usage](docs/usage/running.md) for scaling and save options.

## Controls

| Key | GBA button |
| --- | --- |
| Z / X | A / B |
| Enter / Backspace | Start / Select |
| Arrow keys | D-pad |
| A / S | L / R |
| Escape | Quit |

SDL2 controllers are supported. Battery saves use a `.sav` file next to the
ROM. Supply BIOS and game files you are authorized to use; they are not
included in the project.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

See [testing](docs/development/testing.md) for headless execution, verified test
ROM inputs, sanitizers, and the limits of the current coverage.

## License

Project contributions use GPL-3.0-or-later. See [LICENSE](LICENSE) and
[licensing](docs/legal/provenance.md) for details.
