# BIOS checks

Four `gba.boot-feedback-*` groups check the protected BIOS input left by fast
entry. They cover cartridge and RAM entry, repeated reset, the reset-vector
skip guard, and ordinary firmware latch updates. The read group checks all
16,384 BIOS addresses at byte, halfword and word widths through both fast-entry
routes: 98,304 callback results, including unaligned lane rotations. Each group
retains a ten-second deadline.

The firmware group executes eight signed, unsigned and unaligned first-load
instructions through four preceding low-memory mappings. The 32 owned cases
require the correct first opcode, loaded value, ARM Supervisor control and
next instruction address. A previous remapping supplies deliberately different
RAM code so stale reset prefetch cannot pass as valid firmware execution.

The [original register probes](register-probes.md) also require the four
protected-read comparisons at startup and SWI/IRQ boundaries. Both operands,
their original constant construction, the pre-display result and a completed
terminal branch are required. This qualifies the selected retained input;
physical power-on history and divider phase remain outside this scope.

[Testing overview](../testing.md). Run commands from the repository root.

## Caller return boundaries

The four `gba.bios-dispatch-*` groups execute the unchecked `D4h` callback and
return through the ordinary firmware path. Their 1,024 scenes per BIOS cover
caller flags/masks, stack frames, protected reads, nested division, DMA3 and
timer IRQs. See [SWI dispatch scope](../../hardware/bios/swi-entry.md#unchecked-callback-dispatch).
An optional `CUPID_TEST_BIOS` adds the same groups for a local external image.
These component fixtures retain ten-second deadlines and do not execute boot.

The affine, data-service and register-reset BIOS fixtures use the bounded-step
runner to test their return address after every ordinary guest step. They keep
the original 100,000-step limit and ten-second CTest deadlines. Checking the
return inside the runner avoids a host stack switch after each instruction;
it does not skip guest instructions, peripheral work or result assertions.
A return on the final allowed step succeeds. An initial matching PC, a zero
budget or a stopped machine cannot stand in for an executed return.

`gba.bios-caller-boundaries` compares this path with repeated single stepping in
576 ARM/Thumb cases, including wrong-mode targets, exhausted budgets and the
external execution path. It checks issued-instruction traces, registers,
prefetch, RAM, input-bus history, clocks and timer results. Controls that accept
the initial visit, drop the mode check or shorten the budget are rejected.

## Hard reset and image loading

The sixteen `gba.bios-hard-reset-*` native groups require 1,512 observations of
reset-prefix state, complete built-in startup, timer/DMA/IRQ chronology,
repeated reboots, temporary startup IRQs and ARM/Thumb POSTFLG byte strobes. `CUPID_TEST_BIOS` adds 352
paired prefix/chronology observations. Each group retains a ten-second
deadline. The existing MMIO group adds all byte patterns and both initial
POSTFLG states for BIOS/RAM/ROM origins. Six system-control groups also
cover wider power-port writes, HALT/IRQ and STOP/keypad wake, and zero-input
publication through unused IME bytes. See
[HardReset](../../hardware/bios/hard-reset.md) for complete counts and startup
qualification limits.

The `gba.bios-file-*`, `runner.bios-startup` and `platform.bios-startup` groups
check complete firmware input, state preservation on failure, reset execution
and the startup boundary. The native fixtures and process images use owned
instructions; no BIOS dump is required. See [BIOS image loading](../../hardware/bios/image-loading.md).

## Music frequency, sleep and checksum

Music-frequency fixtures cover caller banks,
fractional interpolation, upper-key clamps, source alignment/waits and live
timer/DMA/IRQ progress, including late callbacks and clock wrap. A local
external BIOS enables paired register, clock and protection comparisons.
Sleep-entrypoint regressions require 1,724 ARM/Thumb cases for HALT, STOP and
CustomHalt. They cover every control byte, ignored upper bits, caller masks
and banks, ready requests, IME, frozen STOP state, external wake filters,
video/audio/timer/DMA progress during HALT, overdue DMA and clock wrap.
An external BIOS adds paired register, timer, IRQ and callback comparisons.
See [sleep entrypoints](../../hardware/bios-sleep.md) for timing and hardware limits.
The 1,560 control observations run in four complete groups; the other 164
state/interaction observations run separately. Every group retains a
ten-second deadline and both BIOS paths use the same partition.
Checksum regressions require 180 observations per BIOS path. They cover all
seven caller banks, IRQ/FIQ masks, ARM/Thumb, RAM/cartridge callers and waits,
changed image words, read-only guest access, timer/DMA progress, deferred IRQs,
overdue callbacks and clock wrap. Eleven groups retain ten-second deadlines
and require their full counts. See [firmware checksum](../../hardware/bios-checksum.md).
BIOS regressions check LZ77 backreferences with bytes whose high bit is set
through both byte and halfword output paths. They also execute ARM/Thumb
interrupt waits, check ready-flag consumption and register preservation,
and distinguish a matching interrupt from an unrelated HALT wake-up.
The wait cases also cover IRQ callers, their saved exception state, both saved
CPSR IRQ masks and either initial IME value. They check the returned request
in R0 and the cleared R3 alongside the preserved registers.

## Data and affine services

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
The BIOS input is not distributed. See [data services](../../hardware/bios-data-services.md)
for the fixtures and qualification limits.

Affine regressions cover 382 background and 1,102 object observations,
including every 16-bit angle for both services. They check complete output,
signed product rounding, wrapped origins, preserved caller state, source and
destination advancement, count/stride cases and memory regions. Four cases
require timer/DMA interrupts, DMA replacement of a future input entry and
sampled output-write chronology. `CUPID_TEST_BIOS` adds comparisons against
the local external BIOS. Background and object groups retain separate
ten-second deadlines, and a mathematical generator checks all sine values.
See [affine services](../../hardware/bios-affine-services.md).

## Sound driver and music player

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
unqualified. See [the sound driver](../../hardware/bios/sound-driver.md).

Thirty-nine `gba.bios-music-player-*` groups cover player lifecycle, all 36
exported entries, owned songs and ARM/Thumb library hooks. Complete state and
execution clocks are checked alongside timer/DMA/IRQ interaction, partial
stores and timestamped FIFO output. `CUPID_TEST_BIOS` repeats every group with
a local authorized image. See [music-player services](../../hardware/bios/music-player.md)
for corpus counts, pointer normalization and the remaining accuracy limits.

## Multiboot

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
[the sender model](../../hardware/bios/multiboot.md) for corpus scope, the separate
full-size profile and unmeasured timing limits. Existing serial source tests
and expected values remain intact.

## Register and soft reset

Register/memory reset checks cover 650 observations across all 256 masks,
both caller modes, ignored upper bits, cartridge waits and safe RAM callers.
They compare complete memory, retained regions, readable I/O, wave banks,
FIFO state, preserved caller state, timers and elapsed clocks. Two clears
require timer/DMA IRQs and sampled DMA writes that the CPU later overwrites.
`CUPID_TEST_BIOS` adds the external comparison, including the sampled chronology.
Each of the sixteen mask groups and the interaction group retains a ten-second
deadline. The existing copy checks also require the observed CpuFastSet R3
data result. See [register reset](../../hardware/bios-register-reset.md).

Soft-reset regressions require 768 observations across every restart byte,
ARM/Thumb state, seven caller banks, three caller memory regions, cartridge
wait settings and DMA before/after restart selection. They check complete
architectural register banks, memory, I/O, elapsed clocks and entry pipeline,
then execute the actual first entry instructions. DMA cases compare sampled
work-area writes and pending timer/DMA requests with IME clear.
`CUPID_TEST_BIOS` adds the external comparisons. Four restart-byte groups,
the caller-state group and the DMA group each retain a ten-second deadline.
See [soft reset](../../hardware/bios-soft-reset.md), including the unresolved
hardware status-register readback edge.

## Math suite

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
[BIOS execution](../../hardware/bios.md). The local ROM and external BIOS are
private inputs and are not required by hosted CI.

## IRQ entry and return

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
[firmware IRQ entry](../../hardware/bios/interrupt-entry.md).
