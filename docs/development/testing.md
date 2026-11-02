# Testing

Published specifications and original test programs define known expectations.
When they leave behavior unresolved, use common established behavior as a
recorded compatibility choice. Keep conflicting hardware-test results visible;
a selected policy does not turn an unresolved measurement into a passing test.

## Build and run

```sh
cmake -S . -B build -DCUPID_BUILD_FRONTEND=OFF
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Use `-DCUPID_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug` on Linux with GCC or Clang
to run address and undefined behavior checks. Compiler warnings are errors.
Default Debug builds retain symbols without enabling optimization. For the
complete cartridge profile, also pass `-DCMAKE_C_FLAGS_DEBUG="-O1 -g"`, as CI
does. This keeps the clocked renderer within the existing observer deadlines;
both sanitizers and fatal error reporting remain enabled. The separate
unoptimized native profile keeps the default Debug flags.

GitHub runs the full matrix for pull requests, pushes to `main`, and tags.
Open a draft pull request for branch qualification. A branch update then has
one matrix run, while the merged result receives its own checks on `main`.

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

## Find a subsystem

- [CPU and interrupt checks](testing/cpu.md).
- [Memory and bus checks](testing/memory.md).
- [BIOS checks](testing/bios.md).
- [Video checks](testing/video.md).
- [Audio checks](testing/audio.md).
- [Cartridge and save checks](testing/cartridge.md).
- [Serial and local-session checks](testing/serial.md).
- [Execution, frontend and runner checks](testing/runtime.md).
- [Timer and DMA suite checks](testing/timers.md).

## Coverage and limits

The native regression executables check event scheduling, clock wrap and reset,
video event deadlines, CPU dispatch coverage, SWI exception entry, arithmetic
boundaries, block transfers, I/O dispatch bounds, signed audio samples, and
affine reference registers. Timer regressions cover delayed register writes,
byte latches and masks, stopped counters, clock-source changes, cascades,
coincident writes and overflow, late dispatch, clock wrap, FIFO refill requests,
and audio synchronization. Checks remain enabled in release builds.

Passing these checks establishes the listed cases. They do not cover every CPU
encoding, every bus or arbitration edge, every timer/IRQ interaction, raster
effects, audio fidelity, cartridge peripherals, save persistence, or link
behavior. Those require further regressions and hardware-backed evidence.

## Hosted and local profiles

Hosted Linux qualification uses the explicit Ubuntu 24.04 release for GCC
Release and Clang Debug with address/undefined-behavior sanitizers. The complete
Clang profile uses `-O1 -g`, matching local cartridge qualification and
[AddressSanitizer's build guidance](https://clang.llvm.org/docs/AddressSanitizer.html#usage).
A second default Debug sanitizer build runs every configured native ARM, core,
GBA, platform and runner check without optimization. The complete profile
retains all available cartridge cases, their original frame counts and process
deadlines. Unoptimized full-cartridge runs timed out on
slower hosted workers; those failures remain recorded rather than increasing
their limits. Both compiler flag sets appear in the job log. The exported
compiler commands also verify effective optimization for every translation
unit, including flags inherited through linked targets. This keeps
an automatic `ubuntu-latest` release migration from changing the compiler,
sanitizer and firmware-assembler baseline. Windows checks use GCC and SDL2.
The Windows configure step selects the same Python interpreter used to fetch
the inputs. This keeps CTest from switching to a separate host installation
when both provide the same Python version. The input-download regression
retains its ten-second limit and eight-second diagnostic traceback.
Both checkout steps pin a verified Node.js 24 action revision. An OS release
upgrade needs the same complete test, firmware regeneration, Make and live
ThreadSanitizer qualification as the current workflow.

On WSL, keep Linux executable output on native Linux storage. Repeated
sanitizer process startup through a Windows mount can dominate short CLI
checks. `CMAKE_RUNTIME_OUTPUT_DIRECTORY` selects executable placement while
preserving source, compiler flags, inputs and deadlines. Preserve exact copies
outside volatile storage; an idle WSL shutdown can clear memory-backed output.
Restore those verified binaries within the same process that runs the checks.

## Internal memory timing

See [internal memory timing checks](testing/memory.md#internal-memory-timing)
for the ten native control-port groups and their qualification limits.
