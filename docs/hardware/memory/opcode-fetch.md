# Instruction fetches across memory regions

An instruction stream can cross a memory boundary without executing a branch.
For example, an ARM refill beginning at `0x02FFFFFC` reads its second word from
`0x03000000`. The first word belongs to external RAM and the second to internal
RAM. Reusing the first region's pointer would instead wrap to the start of
external RAM and execute the wrong instruction.

CupidGBA checks the address against its current direct opcode view on every
sample. Leaving that view selects the addressed mapped device before reading
the opcode. This applies to both words of a branch refill, ordinary sequential
execution and the discarded fetch during timed IRQ entry. ARM words keep their
aligned physical address even when the logical PC retains bit 1; Thumb fetches
use halfwords.

## Mapping and bus ownership

Internal RAM, palette RAM and OAM mirror their existing backing storage within
each 16 MiB address window. [Internal-memory control](internal-control.md) is
decoded before selecting the device. Two virtual windows that select the same
RAM still have separate address bounds, and a control write preserves opcodes
already in the pipeline.

VRAM resolves its bank and bitmap reservation at each value sample. Timed
samples occur after the existing display-port waits; untimed pipeline setup
uses the same address mapping. Crossing the 64 KiB bank boundary, its upper
mirrors or the end of the VRAM window cannot retain the preceding bank's pointer.

The mapped destination selects the existing wait table for each timed request.
External RAM therefore keeps its programmed halfword waits, while internal RAM
uses its 32-bit port. Display contention and ROM page/prefetch rules continue
through their existing bus paths. The untimed refill helper returns its static
first-nonsequential plus second-sequential cost, retaining each fetch's own
device cost when the two addresses select different regions. That return value
does not model live display arbitration or cartridge prefetch.

Refreshing a view does not record a branch, clear the Game Pak queue, replace
queued instructions or release the CPU bus. The sampled opcode still publishes
its [input feedback](read-feedback.md) before a waiting DMA can take ownership.
Mutable cartridge samples take priority over the ordinary mapped value. The
view resolver itself reads no peripheral state and advances no clocks.

## Scope and verification

The `gba.opcode-regions-*` regressions cover ARM/Thumb refills and executed
instruction streams across RAM and display boundaries, internal-memory aliases,
mirrors and destination wait selection. Distinct source and destination
instructions expose an incorrect cached pointer through the resulting guest
registers. The tests also preserve logical PC alignment, branch history and
sampled pipeline state. Three `gba.opcode-ownership-*` groups check mutable
cartridge samples, the discarded IRQ fetch and DMA handover after a crossing.
See [memory tests](../../development/testing/memory.md) for commands.

These checks establish consistency with the existing mapped memory and bus
model. They do not qualify instruction values or physical timing in unmapped,
I/O or save-memory regions. The separate [ROM image bounds](../rom-bus.md) and
[save-fetch prefetch cancellation](../prefetch.md) rules retain their stated
limits. The recorded FIFO and branch/IRQ timing conflicts remain open.
