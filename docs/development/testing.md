# Testing

The native regression executables check event scheduling, clock wrap and reset,
video event deadlines, CPU dispatch coverage, SWI exception entry, arithmetic
boundaries, block transfers, I/O dispatch bounds, signed audio samples, and
affine reference registers. Timer regressions cover delayed register writes,
byte latches and masks, stopped counters, clock-source changes, cascades,
coincident writes and overflow, late dispatch, clock wrap, FIFO refill requests,
and audio synchronization. Checks remain enabled in release builds.

Interrupt regressions cover register visibility, byte masks and acknowledgements,
successive IRQ line changes, HALT wake-up with IME clear, the CPU's sampled
CPSR mask, Thumb exception return, ARM/Thumb MMIO access widths, DMA completion
bits for all four channels, and late dispatch across clock wrap.
CPU regressions check block-transfer bus costs and the final load internal cycle.
BIOS regressions check LZ77 backreferences with bytes whose high bit is set
through both byte and halfword output paths. They also execute ARM/Thumb
interrupt waits, check ready-flag consumption and register preservation,
and distinguish a matching interrupt from an unrelated HALT wake-up.
The wait cases also cover IRQ callers, their saved exception state, both saved
CPSR IRQ masks and either initial IME value. They check the returned request
in R0 and the cleared R3 alongside the preserved registers.

```sh
cmake -S . -B build -DCUPID_BUILD_FRONTEND=OFF
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Use `-DCUPID_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug` on Linux with GCC or Clang
to run address and undefined behavior checks. Compiler warnings are errors.

The optional ROM checks use pinned manifests of CPU, memory, and hardware timer tests.
Fetch the inputs, then configure their location:

```sh
python3 tests/roms/check_rom.py --fetch --directory .local-research/test-roms
python3 tests/roms/check_console.py --fetch --directory .local-research/test-roms
cmake -S . -B build -DCUPID_TEST_ROM_DIR="$PWD/.local-research/test-roms"
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Each input's SHA-256 digest must match the manifest. The runner must reach the
ROM's specified terminal branch and observe zero in its result register. A
timeout, mismatched input, crash, or nonzero test number fails the check. The
runner uses the same CPU and GBA core as the SDL2 application.

The three hardware timer ROMs check reload timing, start/stop timing, and
overflow before the enable reload. The observer reads their visible console
text from VRAM and requires every expected PASS line and the completion message.
It rejects a FAIL line or an incomplete result. It does not modify the ROM,
counter values, or expected results. These checks use the replacement BIOS;
`check_console.py --bios /path/to/gba-bios.bin` can run the same checks with
an external dump.
The observer allows 400 frames with an external BIOS to include its startup
animation, and 20 frames with the built-in path. Both runs require the same
complete visible results.

The optional locally built count-up/IRQ suite has a separate observer:

```sh
python3 tests/roms/check_timer_suite.py --runner build/cupidgba-headless \
  --rom /path/to/timer-suite.gba --bios /path/to/gba-bios.bin
```

The observer requires the pinned input digest, the terminal instruction,
R12=0, exactly 936/936 count-up results and 90/90 IRQ results, and no logged
failures. Set `CUPID_TIMER_SUITE_ROM` when configuring CMake to include the
built-in path in CTest. Set `CUPID_TEST_BIOS` to add the external-BIOS suite
and native wait comparisons. Neither input is committed or required by hosted
CI. The suite checks divided clocks, interrupt handlers, BIOS interrupt waits,
and counter observations inside ARM loops. Local source revisions, compiler
settings and the build recipe stay with the ignored research checkout.

Both BIOS paths pass 936/936 count-up results and 90/90 IRQ results on the same
input. Omit `--bios` from the command above to check the built-in path.
These results establish the tested wait and timer interactions and do not
establish complete BIOS parity.
See [BIOS execution](../hardware/bios.md) for the wait-loop behavior and limits.

To run another authorized input for a fixed number of frames:

```sh
./build/cupidgba-headless /path/to/game.gba --bios /path/to/gba-bios.bin \
  --frames 600 --screenshot .local-research/frame.ppm
```

The headless runner does not load or write disk saves. Its screenshot is the
native 240x160 framebuffer, written as a binary PPM file. Step and emulated-cycle
budgets bound execution; `--expect-loop-register` reports the result register
at an ARM branch to itself. `--terminal-address` restricts that observation to
a specific instruction address.
`--dump-vram FILE` and `--dump-io FILE` write raw observer snapshots for test
result inspection.
`--dump-ewram FILE` provides the result counters and failure log for the local
timer suite observer.

Passing these checks establishes the listed cases. They do not cover every CPU
encoding, exact bus cycles, DMA arbitration, every timer/IRQ interaction, raster
effects, audio fidelity, cartridge peripherals, save persistence, or link
behavior. Those require further regressions and hardware-backed evidence.
