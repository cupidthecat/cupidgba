# Direct Sound FIFO power

`SOUNDCNT_X` bit 7 gates writes to both Direct Sound FIFOs. Byte, halfword
and word stores while master sound is off do not enqueue samples. This also
applies to DMA destination strobes. An accepted DMA still reads its source,
advances its address and count, releases the bus and publishes its completion
interrupt. Sound power does not cancel the transfer.

A transition from enabled to disabled discards queued words in both channels.
Writing the enabled value again preserves the queue. The current implementation
keeps the staged word and held output separate from the queued storage. Its
phase through a power transition remains a hardware qualification limit.
The change does not alter the existing timer, mixer or DMA clock model.

## Regression coverage

The five `gba.fifo-power-*` groups require 160 owned observations:

- 56 ignored writes across both FIFOs, all access widths and byte lanes, at
  four scheduler clock anchors.
- 28 decoded ARM and Thumb byte, halfword and word stores while sound is off.
- 32 complete immediate DMA transfers across both DMA channels, both FIFO
  destinations, either power state and four clock anchors.
- 32 DMA transfers that change power after the first destination strobe,
  checking all remaining payload words, source progress and completion IRQs.
- 12 power cycles through byte, halfword and word control writes, checking
  discarded old words, fresh signed-byte playback and repeated enable writes.

The clock anchors include signed and unsigned 32-bit wrap and a later epoch.
Each group keeps its ten-second deadline. All five groups fail against the
preceding core without changing their assertions.

`CUPID_FIFO_PROBE_DIR` enables nine original local FIFO/DMA cartridges.
`CUPID_TEST_BIOS` repeats them with an external image. The manifest pins each
input digest and its original result-display and terminal addresses. The runner
requires a passing result at both boundaries; display code cannot conceal an
earlier failure by overwriting the result register. No cartridge or BIOS image
is distributed.

The current main-based candidate passes six of eighteen cartridge runs.
The base, prefetch, IRQ, threshold, IRQ-latch and one disable group still fail
on both BIOS paths. Original inputs, comparisons and budgets are retained,
and all failures remain registered when the profile is enabled. The base
cartridge still fails in its second timing case after the power correction.
These results do not qualify the complete FIFO/DMA timing model. Queue capacity,
overflow behavior, powered-on partial writes, staged-word reset, exact refill
thresholds and timer/DMA/IRQ phases need further work. See
[DMA](../dma.md) and [audio timing](timing.md).
