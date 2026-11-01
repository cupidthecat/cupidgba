# User-bank LDM operand conflicts

A privileged ARM user-bank LDM without PC can leave both the user bank and
current bank selected for the following instruction's initial register reads.
The read value is their bitwise OR when those registers have distinct storage.
FIQ has separate R8 through R14; the other privileged modes have separate SP
and LR. Shared registers, User/System execution, ordinary LDM and STM do not
create this extra read value. Architectural writes retain their selected bank.

CupidGBA models this as register-port state in the instruction pipeline.
Instruction-bus wait clocks do not issue another operand phase. A failed
predicate consumes the pending phase. Data and internal phases release the
extra read selection; branch refills, exception entry and reset clear it.
The model adds no instruction, bus or peripheral clocks.

A cartridge-port word access keeps its two halfword strobes and releases the
data operand phase when the high strobe completes. A following STM word then
reads the current bank. This matches ordinary memory transfers even when
the first word writes the cartridge data and direction ports and the second
writes port control.

The initial accumulator ports of MLA and long multiplication precede their
multiplicand phase. Plain MUL reads its multiplicands in the initial phase.
Register-supplied shifts read the shift amount before their internal phase,
then read the data operands. Loads, stores and SWP retain their sampled address
inputs through later phases. SWP keeps one address for both locked accesses,
and samples its store data after the load. ALU flags use the computed result,
without reading the destination through the operand multiplexer again.

Six native groups cover 840 complete transfers across all seven privilege
modes, R0 through R14 and eight data patterns. Further cases check arithmetic,
flags, bank ownership, multiply/accumulate ports, shifts, address writeback,
loads/stores/swap, ARM-to-Thumb branches, predicates, IRQ entry and reset.
Real DMA bus stalls retain complete copied data and timer/DMA requests at three
clock origins, including both counter wraps.

Three GPIO groups add 15,360 paired RAM/port scenes. Each scene issues the
user-bank load and following block store, checks both stored operands and
their bank ownership, and verifies the actual post-store instruction. They
cover all adjacent banked pairs in the five privileged modes, four addressing
modes with and without writeback, every cartridge window, two wait settings,
prefetch enabled and disabled, and four clock epochs. Every group rejects the
preceding port implementation. These are software interaction checks and add
no hardware captures.

An optional local corpus contains eight unchanged cartridges documented as
hardware-tested on GBP. It covers arithmetic, immediate arithmetic, destination
ownership, BX, loads, long multiplication/accumulation and SWP. The observer
requires all 35 original conditional failure guards in order, including each
opcode and the flag that prevents its failure branch. It also requires the
FIQ result before the display code switches banks, and R0 at the actual issued
terminal branch. A System-bank R12 value of zero alone is insufficient.
Owned controls reach zero-result terminals after bypassing failure recording;
the observer must reject the changed input, wrong opcode and missing guard
while preserving the complete observed guest state and clocks.

Configure the original local inputs without copying them into the repository:

```sh
cmake -S . -B build \
  -DCUPID_USER_BANK_PROBE_DIR=/path/to/original-cartridge-directory
cmake --build build --parallel 2
ctest --test-dir build -R 'user-bank-' --output-on-failure
```

`CUPID_TEST_BIOS` repeats all eight cartridges with an authorized external
image. Every input must match its digest. The original 360-frame,
150-million-step and 90-second process guards remain; CTest allows 100 seconds
for each source observer. The existing native groups retain ten-second
deadlines; each GPIO matrix group has a thirty-second deadline.
No firmware, cartridge or external source bytes are distributed.

These observations establish the listed operand cases. Saved-status register
conflicts, every reserved encoding and differences between hardware revisions
remain unqualified. Wider pipeline, prefetch and physical bus timing remain
separate work under the full hardware goal.
