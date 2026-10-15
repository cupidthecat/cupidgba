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

The six `arm.undefined-*` groups require 43,635 owned observations across ARM
and Thumb undefined instructions, unavailable coprocessors, execution regions,
wait controls, privilege banks, masks, predicates and clock wrap. Timed bus
callbacks and static CPU execution each retain the internal cycle. Events
sample the idle interval separately from the two occupied vector fetches;
DMA and timer cases exercise the same interval through the real scheduler.

The cycle sequence is defined in the
[ARM7TDMI Technical Reference Manual](https://documentation-service.arm.com/static/5e8e1323fd977155116a3129),
section 6.18. These checks do not define reserved opcode interpretations or
physical IRQ synchronization and board timing.
