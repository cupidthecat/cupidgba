# Testing

On WSL, keep Linux executable output on native Linux storage. Repeated
sanitizer process startup through a Windows mount can dominate short CLI
checks. `CMAKE_RUNTIME_OUTPUT_DIRECTORY` selects executable placement while
preserving source, compiler flags, inputs and deadlines. Preserve exact copies
outside volatile storage; an idle WSL shutdown can clear memory-backed output.
Restore those verified binaries within the same process that runs the checks.

The five `gba.fifo-power-*` groups cover 160 owned observations of ignored
sound-off writes, ARM/Thumb stores, complete DMA transfers, power edges during
DMA and queued-word disposal. Each keeps a ten-second deadline. The optional
`CUPID_FIFO_PROBE_DIR` points to a local directory containing the nine pinned
FIFO/DMA cartridges. `CUPID_TEST_BIOS` repeats all nine with that image. Both the
original pre-display result, ordered comparisons and terminal must pass. Known
FIFO timing failures remain enabled. See [FIFO power](../hardware/audio/fifo-power.md).

The three `gba.fifo-refill-*` groups retain 1,248 complete level, chronology and
selection cases under ten-second deadlines. Timer consumption loads the staged
word before testing the remaining queue for a DMA refill. Two groups reject
the preceding order; inactive-source controls pass with either order. The
unchanged original IRQ cartridge now reaches its `0x008000AF` comparison and
passes both result boundaries on each BIOS path. Other FIFO timing gaps remain.

Four `gba.fifo-lanes-*` groups cover 256 complete access-width scenes with
ten-second deadlines. They retain every queued word and signed output byte,
occupied-stage behavior, ARM/Thumb I/O request clocks, 32-clock DMA/IRQ histories
and mixed-width output order across both FIFOs and counter crossings. All four
reject the preceding lower-port latching behavior. Original FIFO cartridges,
comparisons and execution guards remain unchanged.

Playback checks keep exact elapsed-frame observations alongside sixty exact
VSync-call duration checks. A frame spin may finish one or two clocks past its
deadline because a CPU instruction must finish; FIFO/DMA arbitration determines
that phase. The call-duration checks independently retain firmware execution
costs. Complete PCM/state signatures, sample counts, source addresses, held
samples and all existing loop budgets remain checked with both firmware paths.

`CUPID_FIFO_SOURCE_ROM` adds the separately rebuilt base cartridge with either
BIOS path. The original source includes a second timer comparison missing from
the bundled image; both inputs remain registered independently. Reproduce it
with `tests/roms/build_fifo_source.py` and a local FASMARM executable as described
in the FIFO guide. The builder checks every source/include byte apart from line
endings, verifies the complete output digest and refuses to replace source files
or a different existing output. Both profiles retain their 360-frame,
150-million-step, 90-second process and 100-second CTest bounds.

The `runner.observations` and three `arm.observer-*` groups require actual
instruction-issue observations rather than a zero terminal alone. They use
owned ARM/Thumb streams with deliberately skipped failure writes, ordered
and repeated observations, malformed inputs, IRQ/HALT/STOP boundaries and
clock wrap. Each retains a ten-second deadline. All twenty prefetch groups
pin their 84 original timing comparison instruction addresses, bytes, modes
and complete register values on each BIOS path. Source inputs, comparisons
and run limits remain unchanged. See
[prefetch qualification](../hardware/prefetch.md).
The bank group observes a user-bank load and both following operand phases,
checks the original bank values and results, and compares observed execution
with execution without the hook across clock wrap. Observation runs before
operand selection and must preserve the pending phase.

Four `gba.prefetch-boundary-*` groups compare 37,632 boundary/miss/cache scene
triples across every ROM window and wait setting, FIFO occupancy and in-flight
phase, full/paused queues, ARM/Thumb request types and four clock epochs.
Owned mapped opcode values, logical ARM A1, exact transfer clocks and final
FIFO state are required. All four reject the preceding implementation; each
keeps a thirty-second deadline. Original cartridge inputs and comparisons
remain unchanged.

Three `arm.user-bank-gpio-*` groups require 15,360 paired RAM/cartridge-port
scenes after actual `LDM^` and `STM` instructions. They cover every adjacent
banked-register pair in the five privileged modes, all block addressing modes
and writeback choices, each cartridge window, wait controls, prefetch and
clock wrap. The first stored word retains the register read-port conflict;
the next uses the current bank after the word's two strobes complete. Every
group has a thirty-second deadline and rejects the preceding implementation.

`CUPID_CPU_INTERACTION_PROBE_DIR` adds sixteen original CPU/DMA/IRQ cartridges
from a local input root. `CUPID_TEST_BIOS` repeats them with the external image.
Every run checks the cartridge digest, its original encoded comparison values
in issue order, the pre-display R12 result and the original terminal. The
twenty comparison instructions also retain their CPU mode and opcode checks.
The 360-frame, 150-million-step and 90-second process guards are unchanged;
CTest allows 100 seconds. These cases expose unresolved IRQ return-address,
HALT/timer and initial display-phase behavior. They remain enabled when the
input root is supplied. See [CPU/DMA/IRQ interactions](../hardware/cpu/dma-irq-interactions.md).

Four `gba.irq-clock-*` timing groups require 2,056 complete waveform clocks through
stretched memory requests and real DMA. They check both fetch widths, all
cartridge wait windows, RAM wait controls, CPU load/store data, repeated input
pulses between ready edges, retained captured stages, internal multiply cycles,
reset and clock wrap. IF and HALT-availability histories keep their original
system-clock deadlines. A preserved preceding core fails all four groups.
Each group has a ten-second deadline. The existing music-progress image and
chronology expectations remain unchanged.
Three additional `gba.irq-clock-gpio-*` groups compare 11,952 complete paired
ordinary-ROM/GPIO word-request waveforms and 388,032 clock observations for
fetches, loads and stores. Both cartridge strobes retain their original
deadlines, while the CPU synchronizer advances at the complete request's final
ready edge. Each group keeps a ten-second deadline. See
[CPU/DMA/IRQ interactions](../hardware/cpu/dma-irq-interactions.md).
The observer group independently rejects bypassed comparisons, wrong opcodes
and modes, and missing visits in ARM/Thumb streams with zero result registers.
Guarded runs match boundary-only observation runs for general and banked
registers, CPSR, the instruction pipeline, EWRAM, IWRAM, I/O and elapsed clocks.
Both paths use the issue hook; this checks those fields rather than full-state
equivalence with execution without a hook.

With `CUPID_HALT_PROBE_DIR` and `CUPID_TEST_BIOS`, twelve additional
`rom.irq-probe-*-external` cases require their 46 original comparison outcomes,
CPU modes, opcodes, pre-display results and actually issued terminals. Original
source and cartridge bytes are pinned; a byte-identical local rebuild locates
the observations without changing the code. One input has a wide conditional
guard encoding that is not a conditional branch on ARM7TDMI. Its observer
checks the preceding comparison's Z flag at the actual issued halfword, so the
guest cannot silently skip failure recording. The slow-return case passes;
the `0xF4` versus `0xF5` comparison remains a failure. The original execution
guards and 90-second process deadline remain, with 100 seconds in CTest.

The two `gba.gpio-byte-*` groups cover 1,536 complete port frames and 576
strobe/wait/timer/IRQ cases. The `rom.rtc-calendar-byte-*` groups retain the
complete calendar matrix, carry trials and negative controls through actual
ARM/Thumb byte instructions, both store-address lanes and both firmware paths.
The earlier halfword/word inputs and expectations remain byte-identical.
Native checks retain ten-second deadlines; guest groups retain thirty-second
deadlines and their original frame/step/process guards. See
[cartridge GPIO](../hardware/cartridge/rtc.md) for behavior and limits.

`gba.rtc-active-transactions` checks 1,968 active/inactive calendar frame and
abort cases plus 3,120 parameter-word overrun/restart cases. Its ten-second
deadline covers complete output/deadline comparisons across clock wrap.
These checks cover the selected logical policy and leave physical serial
propagation and partial-register effects unqualified.

Local-session checks are `gba.cable-*` and `platform.local-*`. They cover
complete paired guest bytes and words, multiplayer receive slots, bus/event
coordination, cancellation, STOP, SDL input/video/audio and independent battery
files. Guest matrix cases have separate CTest entries; each retains its clock
guard and every payload comparison. See [local sessions](../hardware/serial/local-session.md)
for the host API and scope. With `CUPID_TEST_BIOS`, the owned word and UART
guest cases also run with the external firmware. Reconstruct `cable-driver.s`
at `0x03001000` with the same ARMv4T binary-link flags as the UART driver and
verify all 512 bytes with `tests/gba/serial/generate_cable_driver.py`.

Hosted Linux qualification uses the explicit Ubuntu 24.04 release for GCC
Release and Clang Debug with address/undefined-behavior sanitizers. The complete
Clang profile uses `-O1 -g`, matching local cartridge qualification and
[AddressSanitizer's build guidance](https://clang.llvm.org/docs/AddressSanitizer.html#usage).
A second default Debug sanitizer build runs every configured native ARM, core,
GBA, platform and runner check without optimization. The complete profile
retains all available cartridge cases, their original frame counts and process
deadlines. Unoptimized full-cartridge runs timed out on
slower hosted workers; those failures remain recorded rather than increasing
their limits. Both compiler flag sets appear in the job log. This keeps
an automatic `ubuntu-latest` release migration from changing the compiler,
sanitizer and firmware-assembler baseline. Windows checks use GCC and SDL2.
The Windows configure step selects the same Python interpreter used to fetch
the inputs. This keeps CTest from switching to a separate host installation
when both provide the same Python version. The input-download regression
retains its ten-second limit and eight-second diagnostic traceback.
Both checkout steps pin a verified Node.js 24 action revision. An OS release
upgrade needs the same complete test, firmware regeneration, Make and live
ThreadSanitizer qualification as the current workflow.

The ten `gba.uart-*` groups check 12,288 complete format-matrix frames, 124,928
bit-center samples, FIFO capacity, errors, IRQs, connected peers, DMA/timer
chronology and lifecycle. Owned ARM/Thumb guests each require 1,024 transferred
bytes, full RAM output and guards, BIOS HALT wake and their passing terminal.
Two corrupt-input controls must complete and reject the changed byte.
`CUPID_TEST_BIOS` adds three external-image groups. Each retains a thirty-second
deadline and the guest retains its million-step and three-million-clock guards.
Hosted Clang reconstructs the owned driver. See [UART](../hardware/serial/uart.md)
for the nominal baud model and remaining physical qualification.

The six `arm.user-bank-*` groups check 840 complete user-bank transfers, operand
phases, bank ownership, ALU flags, memory and branch behavior, predicates,
reset/IRQ entry and real DMA/timer progress across clock wrap. All retain
ten-second deadlines. `CUPID_USER_BANK_PROBE_DIR` names the local input root
containing the `LDM` directory and adds eight original local
cartridges, and `CUPID_TEST_BIOS` repeats them with the external image. The
observer requires all 35 original failure guards, the FIQ pre-display result
and the issued R0 terminal. Owned negative controls reject bypassed failure
recording while retaining guest state and clocks. See
[user-bank LDM](../hardware/cpu/user-bank-ldm.md) for the scope and input guards.

The eight `gba.joybus-*` groups check complete command/reply data, 1,024 payload
patterns, 10,740 rejected frames, decoded register accesses, IRQ/HALT, DMA/timer
chronology and lifecycle. The owned ARM/Thumb guest completes 256 receive/send
rounds and two failing-input controls per BIOS path; `CUPID_TEST_BIOS` adds the
external-image run. All retain ten-second deadlines. Hosted Clang also rebuilds
the owned assembly fixture. See [JOY commands](../hardware/serial/joybus.md)
for the peer synchronization contract and unmeasured electrical behavior.

The `gba.dma-pages-halfword` and `gba.dma-pages-word` groups require 480 complete
DMA page transactions across source modes, wait banks/profiles, clock wrap
and dispatch granularity. Each keeps a ten-second deadline. The optional
`CUPID_DMA_PROBE_DIR` points to the nine pinned original DMA cartridges.
`CUPID_TEST_BIOS` repeats them with the external image. Both original result
boundaries must pass. All nine external-image runs pass; seven built-in
pause/preemption runs remain failing and registered. The independent
`CUPID_BUS_PAGE_PROBE_ROM` passes all eighteen original CPU/DMA rows on each
BIOS path. See [DMA timing](../hardware/dma.md) for qualification limits.

At preceding head `55058c0d1f2a73bf0210a8377de23c49276d11e8`, the complete
locally configured profile reports 463 passes and 11 failures out
of 474 on Windows GCC and Linux Clang 18 Debug with address/undefined-behavior
sanitizers. It retains all 454 preceding checks and adds two native groups and
eighteen original DMA runs. The eleven failures are the seven built-in DMA
cases above plus the Thumb-to-ARM and binary-only legacy prefetch cartridges
on both BIOS paths. All remain enabled with their original inputs and guards.

The sixteen `gba.bios-hard-reset-*` native groups require 1,512 observations of
reset-prefix state, complete built-in startup, timer/DMA/IRQ chronology,
repeated reboots, temporary startup IRQs and ARM/Thumb POSTFLG byte strobes. `CUPID_TEST_BIOS` adds 352
paired prefix/chronology observations. Each group retains a ten-second
deadline. The existing MMIO group adds all byte patterns and both initial
POSTFLG states for BIOS/RAM/ROM origins. See
[HardReset](../hardware/bios/hard-reset.md) for complete counts and startup
qualification limits.

The six general `arm.undefined-*` groups cover exception-entry clocks, caller banks
and ARM/Thumb return, predicates, clock wrap and the internal-cycle timer/DMA
interval. Five additional `arm.undefined-bkpt-*` groups require 391,436
observations of unsupported breakpoint encodings, exception return, complete
immediates, predicates, explicit host hooks and peripheral progress. Both
timed and static paths retain their opcode fetch costs. All eleven groups
retain ten-second deadlines; see [undefined entry](../hardware/cpu/undefined-entry.md).

The six `arm.cp14-*` groups check decoded register transfers, complete selector
coverage, r15 flags, predicates, retained undefined exceptions and timer/DMA
progress with owned instruction streams. Each has a ten-second deadline;
see [CP14 register transfers](../hardware/cpu/cp14.md).

The four `arm.reset-entry-*` groups check guest-visible reset control, prior
ARM/Thumb and privilege state, pending IRQs, repeated reset and the built-in
fast entry with owned instructions. Each retains a ten-second deadline. Native
BIOS service fixtures prepare their application caller explicitly without
booting firmware or changing peripheral clocks; see [reset entry](../hardware/bios/reset-entry.md).

The `gba.bios-file-*`, `runner.bios-startup` and `platform.bios-startup` groups
check complete firmware input, state preservation on failure, reset execution
and the startup boundary. The native fixtures and process images use owned
instructions; no BIOS dump is required. See [BIOS image loading](../hardware/bios/image-loading.md).

The `gba.cartridge-image-*` groups cover EEPROM address selection, complete
DMA3 serial frames, original image size and replacement ownership. They retain
the ten-second deadlines; see [cartridge image mapping](../hardware/cartridge/image-mapping.md).

The `gba.rom-bus-*` groups cover address-derived data outside the ROM image,
ARM/Thumb loads, DMA rollover and save-device selection. They retain mapped
ROM boundaries and EEPROM output checks. Each group has a ten-second deadline;
see [Game Pak ROM reads](../hardware/rom-bus.md) for the scope.

The `platform.input-*` groups run real SDL2 virtual-controller state through
the frontend event poll. They cover bindings, mixed source ownership, strict
stick boundaries, focus/hotplug and keypad IRQ/STOP interactions. Every group
has a ten-second deadline. See [input handling](../hardware/input.md) for the
complete observations and scope.

Cartridge peripheral checks include `gba.solar-*`, `gba.cartridge-rumble`,
`gba.gyro`, `platform.cartridge-peripherals`, `rom.cartridge-gyro` and
`rom.cartridge-peripherals`. An external BIOS adds paired bus/interaction and
generated-cartridge groups. Native groups
have ten-second deadlines; generated cartridge groups have thirty seconds.
The headless runner can start a probe at `--start-address` (an aligned ARM
cartridge address) and record motor edges with `--dump-rumble`. The output
starts with `CPDRMB01`, followed by twelve-byte records: a little-endian
64-bit emulated clock, a one-byte active flag and three reserved zero bytes.
Registration and removal also report the current/stopped output. Explicit
probe entry does not exercise the BIOS boot sequence. See
[cartridge peripherals](../hardware/cartridge/peripherals.md) for coverage
and physical measurement limits.

Cartridge minute-interrupt regressions check held and steady RTC outputs,
ARM/Thumb control writes, the inverted board edge, IF/IME/HALT sequencing,
timer/DMA ordering, late dispatch, clock wrap and reset. Calendar tests compare
bulk advances with every second; battery and frontend tests preserve the output
latch and avoid replaying IRQs from offline time. These are documented signal
model checks. Physical pulse timing and the remaining cartridge modes are
unqualified; see [RTC behavior](../hardware/cartridge/rtc.md).

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
Serial regressions exercise both widths and clock rates, all 256 transmitted
8-bit patterns, driven input data, external clock edges, two connected core
instances, GPIO directions and falling-input IRQs, ARM/Thumb bus writes,
untouched byte fields, late dispatch, clock wrap, reset and power-state behavior.
CPU regressions check block-transfer bus costs and the final load internal cycle.
DMA regressions check read/write phase boundaries, priority and preemption,
CPU bus stalls, internal-cycle overlap, swap locking, masks, maximum counts,
address stepping, repeat reloads, cancellation, FIFO requests, timed I/O and
clock wrap. Thirty-two additional cases check fixed ROM source pointers with
sequential cartridge data, decrementing ROM-to-OAM transfers, exact completion
clocks, both transfer widths, all three ROM banks and four clock anchors.
They compare late dispatch with advancing one clock at a time.
Prefetch regressions check pending/cached fetches, queue capacity and sustained
bandwidth, full-queue drain/nonsequential restart, an owned timed ROM-data
flush kernel, fill/cancellation clocks, disable/restart behavior, partially buffered
words, both instruction-width changes and BIOS/RAM exception returns,
uncached page boundaries and DMA/timer/IRQ interactions across clock wrap.
They also write wait controls before the first fetch, after a firmware reset
and after an invalid jump, then require the configured costs on a valid fetch.
`CUPID_PREFETCH_PROBE_DIR` points to the root containing the `prefetcher` and
`timing` input subdirectories and enables all twenty unchanged cartridge groups;
`CUPID_TEST_BIOS` adds their external-firmware comparisons. Each requires the
original result before its display routine and at its terminal, a verified
input digest and unchanged frame/instruction bounds. Nineteen groups have
matching source; the additional legacy boundary cartridge has binary evidence
only. The Thumb-to-ARM and legacy boundary failures remain registered.
`CUPID_BUS_PAGE_PROBE_ROM` adds the eighteen-result CPU/DMA page-boundary
cartridge and repeats it when `CUPID_TEST_BIOS` is set. Missing or failing
console rows fail the check; current DMA decrement mismatches remain visible.
See [prefetch](../hardware/prefetch.md).
Music-frequency fixtures cover caller banks,
fractional interpolation, upper-key clamps, source alignment/waits and live
timer/DMA/IRQ progress, including late callbacks and clock wrap. A local
external BIOS enables paired register, clock and protection comparisons.
Sleep-entrypoint regressions require 1,724 ARM/Thumb cases for HALT, STOP and
CustomHalt. They cover every control byte, ignored upper bits, caller masks
and banks, ready requests, IME, frozen STOP state, external wake filters,
video/audio/timer/DMA progress during HALT, overdue DMA and clock wrap.
An external BIOS adds paired register, timer, IRQ and callback comparisons.
See [sleep entrypoints](../hardware/bios-sleep.md) for timing and hardware limits.
The 1,560 control observations run in four complete groups; the other 164
state/interaction observations run separately. Every group retains a
ten-second deadline and both BIOS paths use the same partition.
Checksum regressions require 180 observations per BIOS path. They cover all
seven caller banks, IRQ/FIQ masks, ARM/Thumb, RAM/cartridge callers and waits,
changed image words, read-only guest access, timer/DMA progress, deferred IRQs,
overdue callbacks and clock wrap. Eleven groups retain ten-second deadlines
and require their full counts. See [firmware checksum](../hardware/bios-checksum.md).
BIOS regressions check LZ77 backreferences with bytes whose high bit is set
through both byte and halfword output paths. They also execute ARM/Thumb
interrupt waits, check ready-flag consumption and register preservation,
and distinguish a matching interrupt from an unrelated HALT wake-up.
The wait cases also cover IRQ callers, their saved exception state, both saved
CPSR IRQ masks and either initial IME value. They check the returned request
in R0 and the cleared R3 alongside the preserved registers.

The data-service regressions execute ARM/Thumb callers for SWIs 10 through 19.
They check 2,043 unpacking, compression and differential-filter observations,
including work-RAM and cartridge sources, two cartridge wait-state settings,
complete output and guards, register preservation, timers and elapsed clocks.
A separate 98-case bias check samples register changes during both ramp
directions, preserves every sampling-resolution setting, and runs timer IRQs
and a DMA transfer during a ramp. `CUPID_TEST_BIOS` registers the same checks
against a local external BIOS and compares clocks and caller state directly.
CTest splits the data corpus into 773 EWRAM, 728 IWRAM and 542 cartridge
observations, each with the same ten-second deadline. Every group requires its
full count.
The BIOS input is not distributed. See [data services](../hardware/bios-data-services.md)
for the fixtures and qualification limits.

Affine regressions cover 382 background and 1,102 object observations,
including every 16-bit angle for both services. They check complete output,
signed product rounding, wrapped origins, preserved caller state, source and
destination advancement, count/stride cases and memory regions. Four cases
require timer/DMA interrupts, DMA replacement of a future input entry and
sampled output-write chronology. `CUPID_TEST_BIOS` adds comparisons against
the local external BIOS. Background and object groups retain separate
ten-second deadlines, and a mathematical generator checks all sine values.
See [affine services](../hardware/bios-affine-services.md).

Nineteen `gba.bios-sound-*` groups cover initialization, software mixing,
envelopes, looping, reverb, work-area identifiers, mode fields, frame cadence,
PCM disable/enable, channel clearing, ARM/Thumb callbacks, caller banks, all
twelve playback rates and bus progress during timer/DMA IRQs and clock wrap.
Owned wave fixtures compare complete state apart from six internal address
words, and twelve-frame playback checks include every output sample and its
timestamp. `CUPID_TEST_BIOS` adds matching external-firmware groups. All groups
retain ten-second deadlines and fixed instruction guards; the mixer rate
corpus runs in three four-selector batches with identical inputs and assertions.
Rate fixtures align both video engines explicitly. Physical audio remains
unqualified. See [the sound driver](../hardware/bios/sound-driver.md).

Thirty-nine `gba.bios-music-player-*` groups cover player lifecycle, all 36
exported entries, owned songs and ARM/Thumb library hooks. Complete state and
execution clocks are checked alongside timer/DMA/IRQ interaction, partial
stores and timestamped FIFO output. `CUPID_TEST_BIOS` repeats every group with
a local authorized image. See [music-player services](../hardware/bios/music-player.md)
for corpus counts, pointer normalization and the remaining accuracy limits.

Twenty-seven `gba.bios-multiboot-*` groups validate owned normal and multiplayer
downloads, complete decrypted bodies through the maximum length, source/page
boundaries, selected-client errors, CRC/offset rejection, readiness waits,
disconnection, retained parameters and timestamped wire transcripts. The guard
matrix covers mode/source/length edges, mirrored and read-only parameters and
enabled DMA. Active-download cases observe DMA mutation, pending timer/IRQ
requests and partial cleanup stores. A local
`CUPID_TEST_BIOS` repeats them with the external image. `gba.multiplayer` checks
the word-level device lifecycle, ordered receive slots, clock ownership,
IRQ/cancellation/reset/STOP and synchronized boards. See
[the sender model](../hardware/bios/multiboot.md) for corpus scope, the separate
full-size profile and unmeasured timing limits. Existing serial source tests
and expected values remain intact.

Register/memory reset checks cover 650 observations across all 256 masks,
both caller modes, ignored upper bits, cartridge waits and safe RAM callers.
They compare complete memory, retained regions, readable I/O, wave banks,
FIFO state, preserved caller state, timers and elapsed clocks. Two clears
require timer/DMA IRQs and sampled DMA writes that the CPU later overwrites.
`CUPID_TEST_BIOS` adds the external comparison, including the sampled chronology.
Each of the sixteen mask groups and the interaction group retains a ten-second
deadline. The existing copy checks also require the observed CpuFastSet R3
data result. See [register reset](../hardware/bios-register-reset.md).

Soft-reset regressions require 768 observations across every restart byte,
ARM/Thumb state, seven caller banks, three caller memory regions, cartridge
wait settings and DMA before/after restart selection. They check complete
architectural register banks, memory, I/O, elapsed clocks and entry pipeline,
then execute the actual first entry instructions. DMA cases compare sampled
work-area writes and pending timer/DMA requests with IME clear.
`CUPID_TEST_BIOS` adds the external comparisons. Four restart-byte groups,
the caller-state group and the DMA group each retain a ten-second deadline.
See [soft reset](../hardware/bios-soft-reset.md), including the unresolved
hardware status-register readback edge.

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

The native video group also checks complete frame publication across clock
and counter wrap, late dispatch, STOP, reset and observer removal. A partially
drawn next frame is its negative control: every published pixel must retain
the preceding complete image.

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

The owned timer edge matrix needs no external input:

```sh
cmake --build build --target test_timer_edges --parallel 2
ctest --test-dir build -R '^gba[.]timer-edges-' --output-on-failure
```

Its sixteen channel/prescaler groups retain a 30-second deadline each. They
check all prescaler phases and access widths, stopped-FFFF enable behavior,
cascade observations, IRQ acknowledgement before/at/after overflow and actual
clock-boundary crossings. The expected states use overflow deadlines and
observed register sequences; the fixture never assigns a timer's counter.
See [timers](../hardware/timers.md) for the selected timing and coverage limits.

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

The optional local serial suite retains four counted normal timing results,
90 register observations and four original multiplayer timeouts:

```sh
python3 tests/roms/check_serial_suite.py --runner build/cupidgba-headless \
  --rom /path/to/serial-suite.gba --bios /path/to/gba-bios.bin
```

Set `CUPID_SERIAL_SUITE_ROM` to register the built-in path and `CUPID_TEST_BIOS`
to add the external path. The observer requires its pinned digest, terminal
R12=0, complete counts, every case flag and zero failure logs. The multiplayer
timeouts remain skipped and are never counted as working transfers. The private
wrapper initializes normal data registers between the timing and read suites,
so the read suite starts from its original reset-state precondition; it retains
all timed code and expectations. Additional single-write startup timing failures
are described in [serial behavior](../hardware/serial/normal-and-gpio.md).

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

`platform.context` checks local data across repeated cooperative yields, normal
completion and cancellation. AddressSanitizer builds also require each live
local buffer's boundary to remain poisoned after every return to its stack.
The portable backend uses the sanitizer fiber interface and clears the generic
stack-cleanup marker installed by `makecontext`; otherwise `swapcontext` can
erase redzones belonging to suspended frames. CI runs this test with the native
backend, the portable AddressSanitizer backend and ThreadSanitizer. To check the
portable backend locally:

```sh
cmake -S . -B build-context-portable -DCMAKE_C_COMPILER=clang \
  -DCMAKE_BUILD_TYPE=Debug -DCUPID_SANITIZERS=ON -DCUPID_FAST_CONTEXT=OFF
cmake --build build-context-portable --target test_context --parallel 2
ctest --test-dir build-context-portable -R '^platform.context$' --output-on-failure
```

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

`--expect-register-at ADDRESS REGISTER VALUE` requires an ordered issued
instruction and its live register value. `--expect-register-pair-at ADDRESS
REGISTER VALUE REGISTER VALUE` checks both operands at the same instruction.
The runner allows up to 128 ordered observations. Missing, extra, reordered
or wrong values fail even when both result boundaries contain zero. Records
include actual ARM/Thumb mode and opcode for pinned-input verification.
Observation runs after IRQ arbitration and includes failed ARM predicates;
HALT/STOP polls and discarded exception fetches do not issue instructions.

The `arm.observer-*`, `runner.observations` and `runner.fifo-comparisons`
groups check passive state/clock preservation, IRQ replacement, reset ownership,
branches, failed predicates, complete comparison operands and bypassed failure
writes. Their ten-second deadlines and owned process guards remain fixed.
The FIFO manifest preserves all 26 original comparisons, including the source
base's second `8Bh` comparison that is absent from the bundled image. See
[FIFO power](../hardware/audio/fifo-power.md) for the retained timing failures.


`--checkpoint-address ADDRESS` additionally records the selected register at
the first ARM visit to that address. Success requires both the checkpoint and
terminal values to be zero. An unvisited checkpoint fails, even if later guest
code reports zero. `runner.checkpoint` exercises this with owned instructions.
`--key-event FRAME MASK` changes the pressed-button mask at a completed frame
boundary. Repeat it in frame order to provide a deterministic input sequence;
a zero mask releases all buttons. This uses the same key state and keypad IRQ
path as the frontend.
`--dump-vram FILE` and `--dump-io FILE` write raw observer snapshots for test
result inspection.
`--dump-ewram FILE` provides the result counters and failure log for the local
timer suite observer.
The example explicitly allows at most one instruction per emulated clock for
600 frames (600 Ã— 280896 clocks). The runner's default 50-million-step guard
can stop a valid long run when prefetch permits more instructions per frame.
This changes the observation budget; it does not change emulation timing or
any hardware test expectation.

Set `CUPID_RTC_STATE_ROM` to add `rom.rtc-state`, an optional original RTC
state/status corpus observed through a private RAM text transport. Adding
`CUPID_TEST_BIOS` also registers `rom.rtc-state-external`. The observer requires
the exact prepared input checksum, a complete EWRAM snapshot, the original
360-frame interval and the complete passing result digest for all 17 comparisons
after the presence/write guard. It retains the 150-million-step and 90-second
process limits. The original wire code, delays and comparisons are unchanged;
the cartridge and transport inputs are not fetched or distributed. These cases
cover basic state/status protocol behavior, not calendar or interrupt timing.

`gba.rtc-outputs` checks the documented alarm and selected-frequency model:
all 16 divider clocks, mixed masks across the complete divider phase, output
priority, both hour formats, held alarm output, daily carries and long waits.
`gba.rtc-irq`, `gba.rtc-battery` and `platform.rtc-config` cover parameter stores,
interrupt visibility, repeated edges across timer/DMA work, late dispatch,
clock wrap, reset, battery formats and offline state. These tests preserve
the model's specified behavior; physical cartridge edge/phase timing still
needs qualification. See [RTC behavior and limits](../hardware/cartridge/rtc.md).

`gba.rtc-minute` checks acknowledgement and disable/enable on both sides of
the retained 10 ms carry signal, both minute modes, mode priority, power flags,
clock partitioning and fresh-zero versus actual-carry state. GPIO/IRQ groups
complete ARM/Thumb control bytes at the original strobe deadline during late
dispatch and clock wrap. Battery tests cover version 3 carry retention and
validation, legacy files and offline load without past requests.

`gba.rtc-stop` checks explicit battery time during STOP, frozen system
peripherals, release-edge wake eligibility, resumed IRQ stages, calendar and
clock wrap, SWI return in ARM/Thumb mode and paced-interval equivalence.
Supplying `CUPID_TEST_BIOS` adds the external-firmware path. `rom.rtc-stop`
generates a CupidGBA cartridge that configures the RTC over GPIO, enters STOP
and checks a frozen timer after wake. It uses the replacement BIOS. The runner
reports elapsed battery clocks separately and includes them in its existing
observation budget. These checks do not measure physical oscillator recovery.

Optional standalone serial and HALT probes use their original cartridge bytes
and comparisons. Set `CUPID_SERIAL_PROBE_DIR` for the nine serial IRQ, data and
start-bit probes. Set `CUPID_HALT_PROBE_DIR` and `CUPID_TEST_BIOS` for four HALT
timer/return-address probes that observe the original firmware's internal
addresses. The observer checks each input digest and passing terminal result.
It retains the 360-frame, 150-million-step and 90-second observation budgets.
Native IRQ tests cover the acceptance and wake samples in ARM/Thumb mode,
their saved return addresses and clock wrap.

`gba.irq-pulses` requires queued enabled-request values to survive a later
register update on the same publication clock. It checks complete signal
histories through queue reuse and scheduler wrap, bulk versus single-clock
dispatch, HALT resume in both CPU modes with either mask state, DMA
acknowledgements, STOP and console reset. It keeps the existing IRQ stage
delays and does not replace the original CPU/bus cartridge checks. See
[interrupt ownership and remaining sampling limits](../hardware/interrupts.md).

Set `CUPID_SAVED_STATUS_PROBE_DIR` to the private source directory containing
`psr/psr.gba` to register the saved-status cartridge. `CUPID_TEST_BIOS` adds its
external BIOS path. The checker verifies the pinned 2052-byte input, all five
original ARM comparisons, their operands and privilege modes, the original
display checkpoint and terminal instruction. Both result boundaries must issue
with R12=0. The dedicated probe retains the 360-frame, 150-million-step,
660-frame clock ceiling and 90-second process limits. It does not change the
generic headless runner's observation protocol or distribute the cartridge.

The ten `arm.saved-status-*` groups require 1,139,864 observations across
reads, register/immediate writes, masks, predicates, banks, waits, clock wrap,
static/timed clients and timer/DMA/IRQ interactions. Five
`arm.instruction-observer-*` groups cover initialization/reset ownership,
ARM/Thumb issue, all ARM predicates, HALT/STOP and real timer IRQ preemption.
Their owned programs reach zero-result boundaries even when changed input or
an IRQ return bypasses a required comparison. The observer rejects those runs,
wrong opcodes/modes/privileges/operands, missing or reordered comparisons and
duplicates. Sampled register/bank/status, pipeline, RAM, I/O and clock equality
checks verify passive observation; these samples do not describe every piece
of emulator state. See [saved-status transfers](../hardware/cpu/saved-status-transfers.md).

Passing these checks establishes the listed cases. They do not cover every CPU
encoding, every bus or arbitration edge, every timer/IRQ interaction, raster
effects, audio fidelity, cartridge peripherals, save persistence, or link
behavior. Those require further regressions and hardware-backed evidence.

`gba.tilt` checks complete X/Y samples, command and input ownership, ARM/Thumb
access widths, save isolation, EEPROM transfers, board metadata and command
strobe timing across scheduler wrap. `rom.cartridge-tilt` requires eight
generated drivers to finish all 32 paired observations each, plus disabled
device and invalid input controls. `CUPID_TEST_BIOS` adds
`rom.cartridge-tilt-external`. These use explicit guest entry and initialize
CPU modes and stacks before invoking firmware; they do not qualify BIOS boot.
Platform checks exercise manual tilt input. See the
[sensor model and unqualified timing](../hardware/cartridge/tilt.md).

The four `rom.rtc-calendar-*` groups require complete ARM/Thumb calendar drivers
through both GPIO access widths and all three ROM windows. Independent expected
records cover all one hundred years, month ends, both hour formats, weekday and
minute/second fields. Each of 3,072 records requires all eighteen returned bytes,
complete table/output pointers and a passing terminal. Missing-device and
wrong-expectation controls must finish with their exact expected failure counts.
Separate guest trials poll through a complete RTC second and check the full
year and weekday carry, coupling wire encoding to calendar advancement.
`CUPID_TEST_BIOS` repeats all four groups. Their thirty-second CTest deadlines,
twenty-second process limits and five-million-step/120-frame guards are fixed.
These are explicit-entry drivers; they do not boot firmware or qualify partial
or active-output date writes. See [RTC limits](../hardware/cartridge/rtc.md).

`gba.rtc-parameters-component` observes every sixteen-bit parameter payload
through both command directions, including each partial word, repeated levels,
released input and chip-select abort. `gba.rtc-parameters-bus` checks ARM/Thumb
halfword/word stores in all three GPIO windows, frequency-release IRQ deadlines,
late dispatch, wrap and DMA strobes alongside a timer. Bulk and single-clock
dispatch must agree. `rom.rtc-parameters` runs six owned guest drivers and
requires all 32 IRQ observations per driver, plus all 512 serial-bit comparisons
for each nominal-read driver. `CUPID_TEST_BIOS` adds the external-firmware path.
Missing-device controls fail the positive observations. These inputs use
explicit guest entry and do not qualify BIOS boot. See the
[parameter model and physical limits](../hardware/cartridge/rtc.md).

The `gba.bios-irq-arm`, `gba.bios-irq-thumb` and `gba.bios-irq-events` groups
exercise the installed firmware's fixed IRQ entry/return, complete stack and
caller context, protected readback, DMA/HALT/timer interactions and clock wrap.
`CUPID_TEST_BIOS` adds matching external-firmware groups. The independent
`gba.bios-irq-observer` group checks twenty owned guarded executions.

Set `CUPID_FIRMWARE_IRQ_PROBE_DIR` to the private directory containing the four
original `IRQ_sub*.gba` cartridges. Both firmware paths retain the original
digests, opcode/mode/flag observations, checkpoint, terminal and all limits.
The 360-frame, 150-million-step, 660-frame clock ceiling and 90-second process
guards remain. Every original comparison must issue in order with its expected
flags, and both original result boundaries must issue with R12=0. Current local
results and remaining failures are documented under
[firmware IRQ entry](../hardware/bios/interrupt-entry.md).
