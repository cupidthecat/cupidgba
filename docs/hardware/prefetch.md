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
halfword already buffered waits only for the second halfword. Filling all eight
slots pauses the cartridge burst. Consuming one opcode does not resume filling:
the cached halfwords drain completely, then a nonsequential access starts the
next burst. Internal and data clocks accrue from the scheduler's original
event timestamps, including late DMA dispatch and clock wrap.

RAM transfers leave the Game Pak bus available for prefetch. ROM and save-bus
data transfers stop the stream. A transfer coinciding with a completing
halfword adds the measured arbitration clock. DMA uses the same fill and
cancellation paths; it does not replace the CPU's buffered stream with a
separate cycle estimate. See [DMA and bus ownership](dma.md).

CPU fetch callbacks carry the logical fetch address, including branch pipeline
refills and the discarded IRQ fetch. A matching FIFO entry survives an ARM/Thumb
width change, including an exception return from BIOS or RAM. An unrelated
target starts a new stream. A fetch at a physical 128 KiB ROM
boundary uses nonsequential timing, including a cached or pending FIFO hit.
It takes the ordinary stream-stop path, retaining the existing completing
halfword's cancellation clock. WAITCNT changes update access costs and
track a pending burst restart when prefetch is disabled.

ARM opcode words are read at aligned physical addresses even when interworking
retains bit 1 in R15. The FIFO compares that aligned CPU word query against its
halfword cursor, preserving the cursor and the logical stream independently.
See [CPU instruction addresses](cpu/instruction-addresses.md) for operand and
observation behavior.

A sequential CPU request can continue a cartridge burst before background
filling has received an idle bus clock. A branch refill can join an in-flight
read only after background filling has started, and only at the oldest FIFO
address. A target beyond older queued entries starts a new access. This
distinction uses the CPU's refill request before internal cycles and page
boundaries modify its bus access type.

## Validation

The complete locally built timing suite passes 2020/2020 observations with
both BIOS paths. It exercises ARM and Thumb,
eight ROM wait-state/prefetch combinations, RAM execution, loads/stores, block
transfers, multiplies, shifts, branches, SWIs and DMA timings. Original source
tests and expected values are unchanged. The observer requires the pinned
input digest, its terminal instruction, R12=0 and an empty failure log.

Native tests cover pending and cached fetches, bounded capacity, sustained
fetch bandwidth, complete full-queue draining and nonsequential restart,
reset, RAM fill clocks, external-data cancellation,
halfword arbitration, partially buffered words, both instruction-width changes,
BIOS/RAM exception returns, disable/restart behavior, configured
wait states, idle-bus engagement, oldest-entry branch joining, buffered page
boundaries and DMA/timer/IRQ equivalence between
late dispatch and advancing one clock at a time.

An owned ARM kernel fills the queue with nine RAM loads, performs a ROM data
load and reads a real timer after three ordinary instructions. It requires
the source-observed 110 clocks at three clock anchors. The preceding refill
model produces 104 clocks. Separate unit cases require an idle interval after
the first cached opcode to leave filling paused until the remaining entries
are consumed. The earlier assumption that filling resumed immediately after
one opcode was consumed was incorrect; the cartridge observations determine
the replacement assertion.

`CUPID_PREFETCH_PROBE_DIR` points to the input root containing the `prefetcher`
and `timing` subdirectories. It registers twenty checksum-verified local
cartridge groups: seventeen original prefetch groups, two live WAITCNT groups
and one binary-only legacy boundary group. Nineteen groups have matching
source. `CUPID_TEST_BIOS` repeats every group with the local external
image. Inputs remain private. The observer requires both R12=0 before the
original result display and R12=0 at the original terminal. Some display paths
reuse IWRAM locations that overlap their stack, so the terminal value alone
can conceal the original result. The runner's optional `--checkpoint-address`
records the first ARM visit using the selected `--expect-loop-register`.
Owned positive and negative cartridges check overwritten failures, missing
checkpoints and terminal failures without changing ordinary runner behavior.

All twenty groups additionally require their original timing comparisons at
pinned instruction addresses: 83 assertions from the nineteen source-backed
groups and the binary-only legacy assertion, for 84 observations per BIOS
path. Expectations retain the complete compared register value, including the
timer control halfword in the ten full-word ARM assertions. Setup comparisons
and iterative branch-control comparisons are not timing assertions. The
checker verifies the original instruction bytes, CPU mode, opcode, register
value and complete observation order. An incorrectly calculated branch target
can bypass a failure-recording instruction and still reach a zero result at
both display and terminal; that path cannot satisfy these observations.

The mixed-width case must observe both 0x4C and 0x6A; its neighbor requires
0x28, and the legacy boundary requires 0x29. Each source-backed value was
checked against its unchanged assembly constant and original comparison
instruction. ROM digests, original observations, result boundaries and all
execution guards are retained.

The runner's repeatable `--expect-register-at ADDRESS REGISTER VALUE` option
samples through a read-only CPU instruction-issue hook. It does not sample
HALT polls, STOP, discarded exception fetches or preempted application
instructions. Missing, extra or reordered visits fail, and every requested
value must match. Repeated addresses require one expectation per issued
visit. Owned ARM/Thumb controls deliberately bypass failure recording while
keeping both result boundaries zero; the explicit observations must reject
them. Other controls check repeated visits, unissued terminal instructions,
the exact 128-observation capacity and invalid arguments. CPU tests retain
guest state and clocks with observation enabled, including failed ARM
predicates, branch refills, IRQ arbitration, HALT/STOP, reset and clock wrap.

The current candidate passes all seventeen original prefetch cartridges on
each BIOS path, including all Thumb-only branch groups. The mixed-width case
containing `BX PC` at 0x0800016E now reaches both original 0x4C and 0x6A
comparisons with the required values. Retaining the logical PC's bit 1 while
aligning opcode queries repairs the earlier second-comparison value of 0x66.
No instruction bytes, expected values or timing constants change.

Both live WAITCNT groups pass on each BIOS path with their original 0xBD
timer comparison. These are specific enable/disabled sequences, not a complete
mid-stream write matrix. The binary-only boundary group now reads its encoded
0x29 comparison, repairing the preceding value of 0x21. Its digest, comparison,
pre-display result and terminal are retained; missing assembly limits source
qualification. All forty runs pass, requiring all 84 original observations
per BIOS path. Covered cartridge examples do not establish all page phases.

Two private page-stop experiments, stopping before or after the boundary
halfword, each passed only 34 of the 40 unchanged runs. Both regressed the
first and third boundary cartridges on both BIOS paths and still failed the
legacy comparison. Neither model is retained. Stopping background filling
at the edge also loses the completing halfword's ordinary cancellation clock.
Instead, the boundary demand bypasses both FIFO hit paths and uses the
nonsequential access already selected by the bus. This changes no timing
constant. Background filling and all in-page hits retain their existing rules.

Four `gba.prefetch-boundary-*` groups require 37,632 boundary/miss/cache
scene triples. They cover all six ROM windows, every nonsequential/sequential
wait setting, every FIFO occupancy and in-flight phase, full and
long-paused queues, Thumb sequential/refill requests, both ARM request types,
logical ARM A1 and four clock epochs. Each boundary must match an unrelated
ordinary miss and the configured N transfer plus any existing cancellation
clock. In-page controls retain their cached or pending timing. Owned mapped
opcode data, logical PC, final FIFO state and actual signed/unsigned clock
crossings are checked. Every group rejects the preceding core with its
matching library; each retains its thirty-second deadline. These checks
qualify the selected software ordering without claiming physical capture.

`CUPID_BUS_PAGE_PROBE_ROM` enables a separate fixed CPU/DMA page-boundary
cartridge. The observer requires all eighteen original console results. The
ROM-to-OAM decrement row passes its 63-clock comparison on both BIOS paths.
Both decrement page rows require and receive 70 clocks at 0x0801FFF0 and
0x0803FFF0. All eighteen results remain visible and pass with both BIOS paths.
The programmed DMA source retains its mode; an access ending at a programmed
page boundary requests nonsequential timing for the following ROM strobe.
The independent fixed-source cartridge also passes its original low-byte
timer comparison on both BIOS paths. See [DMA page timing](dma.md) for the
native transactions and the remaining CPU/DMA/IRQ interaction boundaries.

The firmware timing cases execute the built-in math and copy routines through
the timed CPU. See [BIOS execution](bios.md) for their coverage. Both complete
timing checks require all observations to pass.

The additional original slow IRQ-return cartridge passes all three original
comparisons with the external BIOS. CPU-clock IRQ synchronization continues
through bus waits while DMA holds CPU acceptance. The separate strong IRQ
corpus retains an unresolved flag comparison; see
[CPU/DMA/IRQ interactions](cpu/dma-irq-interactions.md).

The full-queue pause/drain/restart and nearby Thumb branch cases above now
have source-cartridge and native coverage. Other refill phases, broader WAITCNT
changes and physical page-edge signal timing need further qualification. The current
disable/drain behavior has native regression coverage but is not fully
qualified on hardware. PPU bus
contention, cartridge peripheral reads used as instructions, and remaining
BIOS services require further work. Passing this matrix does not establish
complete GBA bus or firmware accuracy.
