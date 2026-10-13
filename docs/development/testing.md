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
DMA regressions check read/write phase boundaries, priority and preemption,
CPU bus stalls, internal-cycle overlap, swap locking, masks, maximum counts,
address stepping, repeat reloads, cancellation, FIFO requests, timed I/O and
clock wrap. They compare late dispatch with advancing one clock at a time.
Prefetch regressions check pending/cached fetches, queue capacity and sustained
bandwidth, fill/cancellation clocks, disable/restart behavior, width changes,
uncached page boundaries and DMA/timer/IRQ interactions across clock wrap.
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

The optional ROM checks use pinned manifests of CPU, memory, timer and DMA tests.
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
The downloader retries a connection failure, truncated response or transient
server error at most twice. A changed payload or missing input fails immediately.
Transport retries do not change input digests or test expectations.

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

Four hardware DMA ROMs require ten assertions: immediate sampling at 20 clocks,
two 88-clock burst-restart cases, four latch cases, and three cartridge-crossover
results including its 41-clock duration. They use the same console observer and
reject incomplete or failed results. Setting `CUPID_TEST_BIOS` also registers
these four external-BIOS checks in CTest.

The display status ROM has four interactive menus. Its observer selects each
with button events and requires all twelve original status, VCOUNT, IF and DMA
observations:

```sh
python3 tests/roms/check_ppu.py --fetch --directory .local-research/test-roms
python3 tests/roms/check_ppu.py --directory .local-research/test-roms \
  --runner build/cupidgba-headless --test status
```

The other cases are `vcount`, `irq` and `dma`. Omit `--test` to run all four;
add `--bios` for the external path. Their frame budgets include the original
cycle sweeps and, with an external BIOS, startup. The default instruction limit
is unchanged. `CUPID_TEST_ROM_DIR` registers all four checks, and `CUPID_TEST_BIOS`
adds the external comparisons. See [display timing](../hardware/video-timing.md).

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

The optional local DMA transfer suite has its own observer:

```sh
python3 tests/roms/check_dma_suite.py --runner build/cupidgba-headless \
  --rom /path/to/dma-suite.gba --bios /path/to/gba-bios.bin
```

It requires the pinned input digest, terminal instruction, R12=0, exactly
1244/1244 results and no logged failures. Set `CUPID_DMA_SUITE_ROM` to include
the built-in path in CTest and `CUPID_TEST_BIOS` to add the external path.
The private harness leaves the original tests and expected values unchanged.
Both paths pass. This suite checks transfer data; the four hardware ROMs check
the listed timings and latches. See [DMA and bus ownership](../hardware/dma.md)
for the behavior and remaining limits.

The local CPU and bus timing matrix runs with either BIOS path:

```sh
python3 tests/roms/check_bus_suite.py --runner build/cupidgba-headless \
  --rom /path/to/bus-suite.gba --bios /path/to/gba-bios.bin
```

Set `CUPID_BUS_SUITE_ROM` to add the built-in check to CTest and
`CUPID_TEST_BIOS` to add the external path. The observer
requires its pinned digest, the terminal instruction, R12=0, 2020/2020 observations
and zero logged failures. Both paths pass. See
[prefetch](../hardware/prefetch.md) for coverage and unresolved edges.

The local I/O register-read suite checks all 130 original source expectations:

```sh
python3 tests/roms/check_io_suite.py --runner build/cupidgba-headless \
  --rom /path/to/io-read-suite.gba --bios /path/to/gba-bios.bin
```

The observer requires the pinned input checksum, terminal R12=0, 130/130 results,
all 130 per-case flags, a complete EWRAM snapshot and no logged failures. Omit
`--bios` for the built-in path. Set `CUPID_IO_SUITE_ROM` in CMake to register the
local suite; `CUPID_TEST_BIOS` adds the external path. Native `gba.io` checks
register state and CPU bus reads at each width, ARM/Thumb prefetch, DMA handover
and FIFO reset readback. See [I/O registers](../hardware/io-registers.md).

The optional local audio cartridge observer selects ten unchanged signal tests:

```sh
python3 tests/roms/check_audio.py --runner build/cupidgba-headless \
  --rom /path/to/audio-test.gba
```

Use `--test tone`, `stereo`, `sweep`, `steps`, `dynamic`, `noise`, `wave`, `bias`,
`fifo` or `silence` to run one selection. `CUPID_AUDIO_TEST_ROM` registers each
selection separately in CTest. The observer requires the pinned digest, complete
execution and capture timestamps, and the original source-defined signals.
Software frequency sequences must complete in their declared order and spacing.
Wave and polynomial checks require the complete pattern up to an unknown phase.
The FIFO selection checks every signed source table level, stereo routing and
timer hold changes. It does not establish the complete FIFO/DMA waveform.
Existing instruction limits apply; each selection retains a 120-second process
deadline and a 130-second CTest deadline. The private source revision and build
recipe stay with the research checkout; the ROM is not committed.

The headless runner's `--dump-audio FILE` exports every mixer capture through a
sample sink. It begins with the eight ASCII bytes `CPDPCM01`, followed by packed
little-endian records containing a 64-bit CPU clock and signed 16-bit left and
right values (`<Qhh`, 12 bytes). The sink records frames independently of host
queue overflow. It fails the run on file write or close errors. See
[audio registers](../hardware/audio/registers.md) and
[audio timing](../hardware/audio/timing.md) for coverage and qualification limits.

`platform.audio` checks interpolation and a live SDL2 dummy device with a
concurrent consumer, including a producer slower than playback. The optional
host output sink observes each playback buffer and its source-backed prefix.
The live checks require every source-backed frame to retain the expected PCM;
the slower case must also exercise underrun. The final faded sample is not a
fixed-value observation. Linux CI also runs this check in a separate ThreadSanitizer
build. To reproduce it locally:

```sh
cmake -S . -B build-thread -DCMAKE_C_COMPILER=clang \
  -DCMAKE_BUILD_TYPE=Debug -DCUPID_THREAD_SANITIZER=ON
cmake --build build-thread --target test_audio_output --parallel 2
ctest --test-dir build-thread -R '^platform.audio$' --output-on-failure
```

ThreadSanitizer and AddressSanitizer require separate build directories.

`gba.save-file` checks complete replacements and reloads at every supported
capacity, inferred expansion, explicit type choices, invalid inputs, companion
files and failed writes. Linux uses a temporary file-size resource limit to
exercise a real delayed write failure; the previous save must remain intact and
dirty state must survive. Windows checks replacement while another reader holds
the destination. Fixtures are created in an exclusive test directory and only
those owned files are removed. Failed tests retain their directory for inspection.

`platform.save-startup` uses a small generated ROM to check invalid-save startup,
explicit capacity mismatch, dotted directories and path-length handling. It
requires Python 3 and SDL2. No game, BIOS or save input is distributed. See
[save files](../hardware/cartridge/save-files.md) for behavior and remaining limits.

The local BIOS math suite checks 615 register observations across 123 inputs:

```sh
python3 tests/roms/check_bios_math_suite.py --runner build/cupidgba-headless \
  --rom /path/to/bios-math-suite.gba --bios /path/to/gba-bios.bin
```

Set `CUPID_BIOS_MATH_SUITE_ROM` to add the built-in path and `CUPID_TEST_BIOS`
to add the external comparison. Its observer requires the pinned digest,
terminal instruction, R12=0, 615/615 results and an empty failure log. Both paths
pass without changing the original test inputs or expectations. Native BIOS
checks also cover division and square-root boundaries, fixed-point overflow,
copy/fill alignment and overlap, control masks, selected timing observations,
protected BIOS reads, and a copy interrupted by DMA and a timer IRQ. See
[BIOS execution](../hardware/bios.md). The local ROM and external BIOS are
private inputs and are not required by hosted CI.

To run another authorized input for a fixed number of frames:

```sh
./build/cupidgba-headless /path/to/game.gba --bios /path/to/gba-bios.bin \
  --frames 600 --steps 168537600 --screenshot .local-research/frame.ppm
```

The headless runner does not load or write disk saves. Its screenshot is the
native 240x160 framebuffer, written as a binary PPM file. Step and emulated-cycle
budgets bound execution; `--expect-loop-register` reports the result register
at an ARM branch to itself. `--terminal-address` restricts that observation to
a specific instruction address.
`--key-event FRAME MASK` changes the pressed-button mask at a completed frame
boundary. Repeat it in frame order to provide a deterministic input sequence;
a zero mask releases all buttons. This uses the same key state and keypad IRQ
path as the frontend.
`--dump-vram FILE` and `--dump-io FILE` write raw observer snapshots for test
result inspection.
`--dump-ewram FILE` provides the result counters and failure log for the local
timer suite observer.
The example explicitly allows at most one instruction per emulated clock for
600 frames (600 × 280896 clocks). The runner's default 50-million-step guard
can stop a valid long run when prefetch permits more instructions per frame.
This changes the observation budget; it does not change emulation timing or
any hardware test expectation.

Passing these checks establishes the listed cases. They do not cover every CPU
encoding, every bus or arbitration edge, every timer/IRQ interaction, raster
effects, audio fidelity, cartridge peripherals, save persistence, or link
behavior. Those require further regressions and hardware-backed evidence.
