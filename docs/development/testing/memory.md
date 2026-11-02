# Memory and bus checks

[Testing overview](../testing.md). Run commands from the repository root.

## CPU input feedback

CPU read feedback and its pinned DMA handover probes are described in
[read feedback](../../hardware/memory/read-feedback.md). The native groups are
`gba.memory-feedback-*`; optional cartridge groups are `rom.bus-feedback-*`.
The `word-handover` group checks 768 CPU/DMA transfers from the 32-bit internal
ports, including remapping, protection, rotation, mirrors and clock wrap.
The `word-ownership` group checks observer isolation, unmapped reads and reset
with both timed and legacy cycle-counted CPU word and byte accesses.
The `byte-handover` group adds 512 BIOS/OAM transfers. The `byte-guest` group
executes 256 ARM/Thumb signed and unsigned byte loads, retaining the full
input word independently of narrowing and sign extension in the CPU register.

## Prefetch boundaries

Four `gba.prefetch-boundary-*` groups compare 37,632 boundary/miss/cache scene
triples across every ROM window and wait setting, FIFO occupancy and in-flight
phase, full/paused queues, ARM/Thumb request types and four clock epochs.
Owned mapped opcode values, logical ARM A1, exact transfer clocks and final
FIFO state are required. All four reject the preceding implementation; each
keeps a thirty-second deadline. Original cartridge inputs and comparisons
remain unchanged.

## DMA page transactions

The `gba.dma-pages-halfword` and `gba.dma-pages-word` groups require 480 complete
DMA page transactions across source modes, wait banks/profiles, clock wrap
and dispatch granularity. Each keeps a ten-second deadline. The optional
`CUPID_DMA_PROBE_DIR` points to the nine pinned original DMA cartridges.
`CUPID_TEST_BIOS` repeats them with the external image. Every run requires all ten original comparison sites across the nine inputs,
including both live operands of the mode-change register comparison. The
expected high halfword is checked against its original control-word literal
and preceding shift. Both original result boundaries must also pass. All nine
cartridges pass with both firmware paths. The independent
`CUPID_BUS_PAGE_PROBE_ROM` passes all eighteen original CPU/DMA rows on each
BIOS path. See [DMA timing](../../hardware/dma.md) for qualification limits.

The `runner.dma-comparison-protocol` and `runner.dma-comparison-execution`
checks cover all ten contracts and eleven owned execution streams. They reject
wrong zero measurements, skipped comparisons, altered live expected operands
and equal-but-wrong register pairs even when the guest reports zero. Each
keeps a ten-second deadline. Original cartridges retain their 360-frame,
150-million-step and 90-second process guards.

The comparison decoder covers only the operand forms used by these inputs:
ARM/Thumb immediate CMP and ARM CMP with an unshifted register operand.
The register case also checks the original preceding LSR. See the
[ARM7TDMI instruction summary](https://documentation-service.arm.com/static/5e8e1323fd977155116a3129),
tables 1-2, 1-4 and 1-7, for those instruction forms.

## Earlier integration results

At preceding head `55058c0d1f2a73bf0210a8377de23c49276d11e8`, the complete
locally configured profile reports 463 passes and 11 failures out
of 474 on Windows GCC and Linux Clang 18 Debug with address/undefined-behavior
sanitizers. It retains all 454 preceding checks and adds two native groups and
eighteen original DMA runs. The eleven failures at that revision were seven built-in DMA
pause/preemption cases plus the Thumb-to-ARM and binary-only legacy prefetch
cartridges on both BIOS paths. These historical results are retained; the
current DMA and prefetch outcomes are listed in their sections. All inputs
remain enabled with their original expectations and guards.

## ROM decoding

The `gba.rom-bus-*` groups cover address-derived data outside the ROM image,
ARM/Thumb loads, DMA rollover and save-device selection. They retain mapped
ROM boundaries and EEPROM output checks. Each group has a ten-second deadline;
see [Game Pak ROM reads](../../hardware/rom-bus.md) for the scope.

## Original prefetch cartridges

`CUPID_PREFETCH_PROBE_DIR` points to the root containing the `prefetcher` and
`timing` input subdirectories and enables all twenty unchanged cartridge groups;
`CUPID_TEST_BIOS` adds their external-firmware comparisons. Each requires the
original result before its display routine and at its terminal, a verified
input digest and unchanged frame/instruction bounds. Each firmware path also
requires all 84 original comparison observations. Nineteen groups have
matching source; the additional legacy boundary cartridge has binary evidence
only. All twenty groups pass on both firmware paths. Fetch the pinned inputs
and their verified license notice with:

```sh
python3 tests/roms/check_prefetch_probe.py --fetch --directory .local-research/test-roms
cmake -S . -B build -DCUPID_PREFETCH_PROBE_DIR="$PWD/.local-research/test-roms"
ctest --test-dir build -R '^rom\.prefetch-' --output-on-failure
```

Linux and Windows CI fetch and run these twenty cartridge groups with the
built-in firmware. `rom.prefetch-inputs` checks fetching, input selection,
checksum rejection and license retention without network access. Downloads use
the shared bounded transport retry path; an altered payload fails immediately.
The original 360-frame, 150,000,000-step and 90-second execution guards remain
unchanged, with a 95-second CTest limit per cartridge. These cases do not cover
every stream transition or close the separate CPU/DMA/IRQ timing gaps.

## Original bus-page cartridge

`CUPID_BUS_PAGE_PROBE_ROM` adds the eighteen-result CPU/DMA page-boundary
cartridge and repeats it when `CUPID_TEST_BIOS` is set. Missing or failing
console rows fail the check; both DMA decrement rows retain their original comparisons.
See [prefetch](../../hardware/prefetch.md).

## CPU and bus timing suite

The local CPU and bus timing matrix runs with either BIOS path:

```sh
python3 tests/roms/check_bus_suite.py --runner build/cupidgba-headless \
  --rom /path/to/bus-suite.gba --bios /path/to/gba-bios.bin
```

Set `CUPID_BUS_SUITE_ROM` to add the built-in check to CTest and
`CUPID_TEST_BIOS` to add the external path. The observer
requires its pinned digest, the terminal instruction, R12=0, 2020/2020 observations
and zero logged failures. Both paths pass. See
[prefetch](../../hardware/prefetch.md) for coverage and unresolved edges.

## I/O register suite

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
and FIFO reset readback. See [I/O registers](../../hardware/io-registers.md).

## Internal memory timing

The ten `gba.ewram-wait-control-*` groups cover the mirrored control port,
writable and reserved lanes, ARM/Thumb stores, live EWRAM instruction execution,
CPU data timing, both DMA directions, decoder boundaries and reset/instance
ownership. Run them with `ctest --test-dir build -R gba.ewram-wait-control
--output-on-failure`. These are owned software regressions. Value 15 checks the
selected readback/timing policy; it does not qualify physical RAM failure.
See [internal memory timing](../../hardware/memory/ewram-wait-control.md) for the
supported fields and remaining RAM-control gaps.

## Native CPU and DMA coverage

CPU regressions check block-transfer bus costs and the final load internal cycle.
DMA regressions check read/write phase boundaries, priority and preemption,
CPU bus stalls, internal-cycle overlap, swap locking, masks, maximum counts,
address stepping, repeat reloads, cancellation, FIFO requests, timed I/O and
clock wrap. Thirty-two additional cases check fixed ROM source pointers with
sequential cartridge data, decrementing ROM-to-OAM transfers, exact completion
clocks, both transfer widths, all three ROM banks and four clock anchors.
They compare late dispatch with advancing one clock at a time.

## Native prefetch coverage

Prefetch regressions check pending/cached fetches, queue capacity and sustained
bandwidth, full-queue drain/nonsequential restart, an owned timed ROM-data
flush kernel, fill/cancellation clocks, disable/restart behavior, partially buffered
words, both instruction-width changes and BIOS/RAM exception returns,
uncached page boundaries and DMA/timer/IRQ interactions across clock wrap.
They also write wait controls before the first fetch, after a firmware reset
and after an invalid jump, then require the configured costs on a valid fetch.

## Internal memory mapping

The seven `gba.internal-memory-control-*` groups check the low control latch
and all four documented device maps. Register checks cover all 256 I/O banks,
all low-byte values and byte/halfword/word writes. Storage checks compare both
complete RAM buffers after each write, read back all lanes and check aliases
through the end of each 16-MiB window. BIOS checks retain protection and reject
writes in both locations.

Timed reads, writes and fetches cover every selected RAM window at four clock
epochs. Another 64 block-transfer cases cover all four address directions,
sparse register lists, physical RAM wrapping, virtual writeback and timed or
untimed cycle accounting. The pipeline group executes 480 ARM/Thumb mapping
changes, covering all
three store widths and transitions between distinct mapped devices. It checks
that the two queued instructions survive while later instructions come from
the new device. DMA checks include both widths, valid and protected sources,
all RAM destinations, untouched trailing bytes and the exact release clock.
Halfword and opcode feedback retain the other internal-RAM input bank.

Run `ctest --test-dir build -R gba.internal-memory-control --output-on-failure`.
Each group has a ten-second deadline. All seven groups reject the preceding
core. These tests qualify the selected documented model; they are not physical
measurements of remap propagation or undocumented control-bit effects. See
[the mapping model](../../hardware/memory/internal-control.md).
