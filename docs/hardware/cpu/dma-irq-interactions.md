# CPU, DMA and IRQ interactions

The optional `CUPID_CPU_INTERACTION_PROBE_DIR` profile runs sixteen checksum-pinned
local cartridges. The input directory contains their `Interactions` folder.
No cartridge or firmware image is distributed. `CUPID_TEST_BIOS` adds the
external-firmware run of each case.

The cases combine HBlank DMA with CPU memory accesses, internal multiply cycles,
branches, RAM execution, timer IRQs and HALT. The observer requires all twenty
original comparison values in order at the original issued ARM or Thumb
instructions. It checks the mode and encoded opcode as well as the register
value. Both the pre-display R12 result and original terminal must report zero.
The original input bytes, comparisons and execution guards remain unchanged.

The selected fast entry aligns its display line position with full firmware
boot at first cartridge issue; see [reset entry](../bios/reset-entry.md). The
previous early display phase explains the formerly built-in-only failures
across internal-cycle, branch, store and DMA pause/preemption cases. This is an
initial-state correction, with no additional IRQ latency or bus timing credit.
The independent startup waveform groups retain the complete status, request
and frame-counter chronology through two frame periods and clock wrap.

The ordinary HALT/DMA case still reaches its encoded return address but reads
`0x88` instead of `0x87` with either firmware path. The OAM variant additionally
depends on display-memory arbitration: the CPU/bus stack reads `0xDC` instead
of `0xDD`, while the pixel stack satisfies that comparison. These remaining
boundaries stay enabled in their respective profiles. A passing startup phase
does not establish every power-on register, timer-divider phase or physical
IRQ sampling edge.

The CPU samples its synchronized IRQ input at instruction boundaries. Its
synchronizer keeps two CPU-clock stages after register acceptance. DMA bus
waits and the intermediate clocks of stretched memory accesses hold those
stages. The transfer's final ready edge completes one CPU clock. Internal
cycles and HALT continue to clock the stages during DMA. Peripheral IF and
HALT-availability deadlines are unchanged.

A short input level that changes back before a CPU-ready edge is not captured;
a previously captured stage survives the wait. Native waveform checks retain
both cases across repeated pulses and clock wrap. The ready edge has its own
scheduler event, preserving peripheral dispatch partitions and the existing
music-player progress snapshots. The
[ARM7TDMI memory-interface description](https://documentation-service.arm.com/static/5e8e1323fd977155116a3129)
distinguishes a stretched bus cycle from its memory clocks. The cartridge
comparisons qualify the listed GBA interactions; they do not establish every
input sampling phase or interrupt configuration.

The separate twelve-cartridge IRQ profile uses the external firmware and
requires 46 original comparison outcomes at issued instruction sites. The
slow-return cartridge now completes its three original comparisons and
terminal. A preserved preceding core reaches only ten passing cartridges
under these checks; the candidate reaches eleven. One input still reads
`0xF4` versus `0xF5`. Its original guard uses a wide conditional encoding,
whose first halfword ARM7TDMI issues as a BL prefix. A zero guest result had
hidden the failed comparison. The host observer checks the preceding Z flag
at that issued site and rejects the result. Input bytes, modes and instruction
semantics remain unchanged; this profile does not add a wider instruction set.

The separate timer/IRQ suite, page-boundary tests and native signal histories
remain useful checks of their covered cases. They do not supersede these
failures. The CPU/bus and DMA acceptance remain open in #10 and #8. See
[interrupt ownership](../interrupts.md), [DMA](../dma.md) and
[Game Pak prefetch](../prefetch.md).

CPU-ready sampling, peripheral IF acceptance and CPU register writes use
separate scheduler phases. A new level accepted on the completing ready edge
enters the following CPU clock. Queue insertion time cannot change that sample.
The `gba.irq-clock-ordering` group checks 48 complete waveforms and 384 clocks
across IWRAM, EWRAM and ROM, both fetch widths and four clock epochs. It queues
the identical IF deadline before a request and during its wait. IF acceptance,
HALT availability and the final CPU line must retain their original deadlines
in both arrangements. The preserved preceding core rejects this regression.

GPIO word requests retain two timed 16-bit cartridge strobes and one CPU-ready
edge. Releasing the synchronizer at both strobes previously advanced an
unrelated timer IRQ one clock early. The three `gba.irq-clock-gpio-*` groups
compare complete ordinary-ROM and GPIO histories for opcode fetches, loads and
stores: 11,952 paired waveforms and 388,032 clock observations across all three
cartridge windows, wait controls, both request types, prefetch enabled/disabled
and four clock epochs. They retain the same total request clocks, IF acceptance
and HALT availability. The preserved preceding core fails all three groups.
This checks consistency within the CPU-ready model; it does not establish
physical GPIO propagation delays.
