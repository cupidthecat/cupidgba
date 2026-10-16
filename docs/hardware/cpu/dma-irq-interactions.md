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

The current candidate passes sixteen of the thirty-two runs: fourteen with the
external BIOS and two with built-in firmware. Sixteen runs fail. Four external
cases now match their encoded IRQ return addresses: the ordinary load, branch
and two store cases. Two external HALT
cases reach the correct return address but read timer values `0x88` versus
`0x87`, and `0xDC` versus `0xDD`. These are unresolved failures. The built-in
results additionally depend on initial display phase; they do not qualify
that phase or justify changing a fixed startup offset.

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
