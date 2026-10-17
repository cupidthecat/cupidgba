# Building

Local console sessions can use `libboost-context-dev` on Linux for native
stack switching. CMake detects it without adding C++ source to the project.
Without it, or with `CUPID_FAST_CONTEXT=OFF`, the portable backend remains
available. Windows uses its native fiber API. See [local sessions](../hardware/serial/local-session.md).

The emulator uses C11 and SDL2 2.24 or later. The input regressions use SDL2's
extended virtual joystick API. The core and headless test runner have no SDL2
dependency. CMake keeps generated files in the chosen build directory. The
Makefile is also supported on Linux and tracks header dependencies.

On Debian or Ubuntu, install `build-essential`, `cmake`, `pkg-config`,
`libsdl2-dev`, and `python3`, then run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
./build/cupidgba /path/to/game.gba -b /path/to/gba-bios.bin
```

On Windows, open an MSYS2 UCRT64 terminal and install the matching packages:

```sh
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2 \
  mingw-w64-ucrt-x86_64-python
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
./build/cupidgba.exe /path/to/game.gba -b /path/to/gba-bios.bin
```

For a core build without a windowing dependency, add
`-DCUPID_BUILD_FRONTEND=OFF`. See [testing.md](testing.md) for regression checks
and sanitizer builds. Supported CI builds cover Linux GCC, Linux Clang with
address and undefined behavior sanitizers, and Windows GCC with SDL2.
