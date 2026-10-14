# Game Pak prefetch

WAITCNT bit 14 enables a 16-byte opcode buffer. Cartridge reads can fill it
while the CPU performs internal cycles or accesses RAM and I/O. A buffered
opcode takes one CPU fetch clock. A fetch that reaches an in-flight cartridge
read waits for its remaining clocks; a miss uses the configured ROM wait states.
The hardware buffer holds eight halfwords, as shown in the
[programming manual, pages 16 and 21](https://www.manuallib.com/download/2023-10-17/AGB%20Programming%20Manual.pdf#page=16).

The core tracks eight halfword slots and the current cartridge halfword read's
remaining clocks. ARM fetches consume two slots and Thumb fetches consume one.
Each halfword becomes available independently. A word fetch with its first
halfword already buffered waits only for the second halfword. Once a full queue
drains, subsequent fetches return to the
cartridge's rate. Internal and data clocks accrue from the scheduler's original
event timestamps, including late DMA dispatch and clock wrap.

RAM transfers leave the Game Pak bus available for prefetch. ROM and save-bus
data transfers stop the stream. A transfer coinciding with a completing
halfword adds the measured arbitration clock. DMA uses the same fill and
cancellation paths; it does not replace the CPU's buffered stream with a
separate cycle estimate. See [DMA and bus ownership](dma.md).

CPU fetch callbacks carry the actual fetch address, including branch pipeline
refills and the discarded IRQ fetch. A matching FIFO entry survives an ARM/Thumb
width change, including an exception return from BIOS or RAM. An unrelated
target starts a new stream. An uncached fetch at a 128 KiB ROM
boundary uses nonsequential timing. WAITCNT changes update access costs and
track a pending burst restart when prefetch is disabled.

## Validation

The complete locally built timing suite passes 2020/2020 observations with
both BIOS paths. It exercises ARM and Thumb,
eight ROM wait-state/prefetch combinations, RAM execution, loads/stores, block
transfers, multiplies, shifts, branches, SWIs and DMA timings. Original source
tests and expected values are unchanged. The observer requires the pinned
input digest, its terminal instruction, R12=0 and an empty failure log.

Native tests cover pending and cached fetches, bounded capacity, sustained
fetch bandwidth, reset, RAM fill clocks, external-data cancellation,
halfword arbitration, partially buffered words, both instruction-width changes,
BIOS/RAM exception returns, disable/restart behavior, configured
wait states, uncached page boundaries and DMA/timer/IRQ equivalence between
late dispatch and advancing one clock at a time.

The firmware timing cases execute the built-in math and copy routines through
the timed CPU. See [BIOS execution](bios.md) for their coverage. Both complete
timing checks require all observations to pass.

An additional original IRQ-return cartridge still times out with an external
BIOS because its wait loop accepts an earlier return address than it expects.
The FIFO correction preserves matching Thumb entries at the later exception
return; it does not establish the earlier IRQ sampling point. This failure
remains unresolved, with the original ROM, comparisons and run budget retained.

Full-buffer backpressure, mid-stream WAITCNT changes and prefetched page-boundary
edges need more hardware captures. The current disable/drain behavior has
native regression coverage but is not fully qualified on hardware. PPU bus
contention, cartridge peripheral reads used as instructions, and remaining
BIOS services require further work. Passing this matrix does not establish
complete GBA bus or firmware accuracy.
