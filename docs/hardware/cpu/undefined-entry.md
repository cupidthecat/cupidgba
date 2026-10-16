# Undefined-instruction entry

CupidGBA accounts for the undefined-instruction sequence as `2S + 1I + 1N`.
The source instruction completes its sequential fetch, an internal interval
advances while the bus is free, and the Undefined vector receives its
nonsequential and sequential fetches. With one-clock memory accesses this
takes four clocks. SWI entry uses its separate path.

The internal interval runs before the active bank and instruction state change.
Timer and DMA events therefore see the caller context during that interval.
Vector entry then selects Undefined mode, clears Thumb state, masks IRQ and
preserves the previous FIQ mask. SPSR and LR retain the caller status and next
instruction address. A `MOVS pc,lr` handler restores the caller's state.

The six general `arm.undefined-*` groups require 43,635 owned observations across ARM
and Thumb undefined instructions, unavailable coprocessors, execution regions,
wait controls, privilege banks, masks, predicates and clock wrap. Timed bus
callbacks and static CPU execution each retain the internal cycle. Events
sample the idle interval separately from the two occupied vector fetches;
DMA and timer cases exercise the same interval through the real scheduler.

## Unsupported breakpoint encodings

The default GBA callbacks send ARM `0xE1200070` and Thumb `0xBE00` breakpoint
encodings, including their immediate fields, through Undefined entry at vector
4. The ARM7TDMI instruction set does not include `BKPT`; the
[assembler guide, section 4.10.1](https://documentation-service.arm.com/static/5e9831ef9931941038dd07ba)
lists it for ARMv5 and later. Caller banks, CPSR/SPSR, return address and the
internal interval follow the same path as other unavailable instructions.
ARM predicates still determine whether the instruction executes.

An embedding debugger can explicitly replace `irqh.bkpt16` or `irqh.bkpt32`.
Such a callback receives the complete immediate and controls whether to trap
or continue. The opcode fetch retains its ordinary timing in either case,
including static CPU execution without timed memory callbacks.

Five `arm.undefined-bkpt-*` groups require 391,436 observations. They execute
all 65,536 ARM and 256 Thumb immediates, check entry and return across nine
execution regions, four wait controls, seven privilege modes, all flags/masks
and clock wrap, and exercise explicit host hooks with every immediate on
both timed and static paths. Predicate controls cover every condition and
flag pattern. Timer/DMA events observe the caller during the internal
interval and verify the completed transfer payload before vector entry.
All eleven Undefined groups retain ten-second deadlines.

The cycle sequence is defined in the
[ARM7TDMI Technical Reference Manual](https://documentation-service.arm.com/static/5e8e1323fd977155116a3129),
sections 2.8.8 and 6.18. Other reserved opcode interpretations, physical IRQ
synchronization and board timing remain separate qualification boundaries.
