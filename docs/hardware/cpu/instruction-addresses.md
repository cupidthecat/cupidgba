# CPU instruction addresses

CupidGBA preserves bit 1 in the logical ARM PC after a PC write, including a
Thumb `BX PC` whose destination is two bytes past a word boundary. ARM opcode
bytes come from the aligned word address without rotation. Normal unaligned
word data loads retain their separate rotation behavior. PC operands use the
logical address with the instruction's pipeline offset.

Game Pak opcode timing retains the logical stream address. A word request
compares its aligned query with the FIFO's original halfword cursor. Aligning
the cursor as well would join a different cartridge stream. This distinction
changes no wait-state constants and adds no instruction-specific delay.

The 128 KiB cartridge page decoder uses the physical opcode address, before
choosing the nonsequential access at a page's first word. For example, logical
ARM address `0x08020002` fetches the word at `0x08020000`; retained A1 must not
hide that page restart. The logical address and original halfword FIFO cursor
remain intact. This separates page decoding from stream matching.

The instruction observer runs at issue, after IRQ/HALT arbitration. At that
boundary R15 is ahead of the pending instruction by four bytes in ARM state or
two bytes in Thumb state. `cupid_arm_instruction_address` returns its physical
opcode address; the observed register file still contains the logical R15.
The helper does not apply inside an executing instruction, where R15 has
advanced again. Runner comparison probes, checkpoints and terminal detection
use physical opcode addresses.

Four `arm.pc-address-*` groups cover 129 owned trials:

- Refill checks all four combinations of address bits 0 and 1 in IWRAM,
  EWRAM and ROM, with
  prefetch enabled and disabled. It requires both unrotated pipeline words,
  retained logical bit 1 and executed register results.
- Operand checks execute ADD/MOV with PC, unaligned LDR and stored results,
  plus MOV-to-PC, LDR-to-PC and ARM BX entry.
- Interworking executes Thumb BX-register and BX-PC, ARM PC operands, data
  loads/stores and a return to Thumb. Both prefetch settings and four clock
  anchors cover signed and unsigned clock wrap and a later epoch.
- Observation checks the exact physical address, logical R15, opcode, state
  and order of seven issued instructions. Hook-free and observed machines
  retain identical registers, flags, pipeline, RAM and clocks at each step.

All four groups reject the archived preceding core with its matching headers
and library. The runner also executes an owned BX-PC cartridge whose ARM
comparison, checkpoint and terminal addresses differ from its logical PC.
The original unchanged mixed-width cartridge now passes both 0x4C and 0x6A
comparisons, its pre-display result and terminal with either BIOS path. The
full prefetch profile now passes 40/40 after the buffered page restart fix.
These results qualify the covered address sequences, not complete
CPU or cartridge timing accuracy.

The additional `arm.pc-address-page` group checks 96 executed two-instruction
programs, 96 crossing refills and 96 direct request pairs with prefetch disabled.
Three wait-state banks, four WAITCNT settings, both logical A1 values and four clock epochs
require 384 exact clock comparisons. Boundary words use nonsequential timing;
the following words retain sequential timing. Executed PC operands, logical
R15, physical issue addresses and both complete pipeline words remain checked.
The preceding core fails this group with its own headers and library. A separate
diagnostic records 80 failing clock comparisons before the fix and zero after
it. Existing mixed-width inputs and all four preceding address groups remain
unchanged. The four additional buffered page groups in
[Game Pak prefetch](../prefetch.md#validation) retain logical ARM A1 while
forcing the physical boundary's N transfer through the ordinary stop path.
