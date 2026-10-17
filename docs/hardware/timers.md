# Timers

Reload writes update a separate latch one cycle after the bus write. Control
writes are also delayed. An overflow on that same cycle uses the previous
reload and control, then propagates its interrupt, FIFO request, and cascade
tick. Byte reload writes merge with the latch; the reserved control byte has
no effect. Timer 0 ignores the cascade bit.

Independent timers use the shared prescaler phase. Disabling freezes the
counter. Changing an enabled timer's clock source preserves its counter;
enabling a stopped timer reloads it and delays the first counting edge. A
stopped counter at FFFF can overflow before that reload, producing both an
interrupt and a cascade tick.

The selected enable model retains that FFFF counter when control acceptance
coincides with the shared prescaler boundary. Its next divided tick overflows
and loads the reload latch. At other phases, enabling loads the latch and
delays the counting epoch by one clock. The same policy applies at /1, /64,
/256 and /1024; changing an already enabled timer still preserves its counter.

Events use timestamps since power-on. Late dispatch replays each overflow
and its side effects instead of reducing the counter modulo its period.
FIFO refill requests retain their first pending deadline and transfer four
words. Disabling the DMA channel cancels its pending refill.

The automated hardware ROMs verify ten assertions for reload, start/stop,
and the pre-reload overflow at the unscaled clock. Host tests cover other
prescalers, all four channels, clock-source changes, cascades, wraps of the
32-bit scheduler clock, and delayed FIFO/mixer dispatch. See
[testing](../development/testing.md) for commands and result protocols.

A locally built suite with an external BIOS also passes 936 count-up
assertions and 90 timer-IRQ assertions. It exercises divided clocks, tight and
loose ARM polling loops, and repeated interrupts. The built-in BIOS path
passes the same assertions; see the result protocols in the testing guide.

MMIO accesses synchronize peripheral events at the end of the data-bus cycle.
[Interrupt timing](interrupts.md) describes register acceptance and CPU sampling.
Sixteen `gba.timer-edges` groups check every phase of every prescaler on all
four channels, byte/halfword/word writes, cascades and four clock epochs.
They check 64,560 enable scenes and 576 IRQ acknowledgement scenes, with
1,036,032 sampled counter, control, cascade, IF, availability and line states.
The acknowledgement scenes distinguish the preceding clock, an overflow's own
clock and the following clock. They also retain a line pulse already in flight
after IF is cleared. Wrap checks require the actual scheduler boundary crossing.

The divided-clock policy and request ordering are source-selected behavior.
These checks establish that logical model and preserve the original hardware
ROM expectations; they do not add a physical capture of the disputed edges.
DMA competes at individual transfer phases and retains overflow timestamps;
see [DMA and bus ownership](dma.md) for arbitration and remaining limits.
