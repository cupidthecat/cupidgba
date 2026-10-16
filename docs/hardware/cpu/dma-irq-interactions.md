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

The current candidate passes twelve of the thirty-two runs: ten with the
external BIOS and two with built-in firmware. Twenty runs fail. Four external
cases record an IRQ return address one Thumb instruction before the encoded
expectation: the ordinary load, branch and two store cases. Two external HALT
cases reach the correct return address but read timer values `0x88` versus
`0x87`, and `0xDC` versus `0xDD`. These are unresolved failures. The built-in
results additionally depend on initial display phase; they do not qualify
that phase or justify changing a fixed startup offset.

The current CPU samples IRQ availability at instruction boundaries. DMA stalls
its bus requests while peripheral events continue; internal CPU cycles can
overlap an active transfer. These rules do not yet qualify the CPU's IRQ
sampling phase through every stall. A private experiment pausing pending
IRQ-line publications during bus stalls changed none of these thirty-two
results. That model was not included in the candidate.

The separate timer/IRQ suite, page-boundary tests and native signal histories
remain useful checks of their covered cases. They do not supersede these
failures. The CPU/bus and DMA acceptance remain open in #10 and #8. See
[interrupt ownership](../interrupts.md), [DMA](../dma.md) and
[Game Pak prefetch](../prefetch.md).
