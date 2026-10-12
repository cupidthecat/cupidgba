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
loose ARM polling loops, and repeated interrupts. The built-in BIOS path fails
215 of those count-up assertions; see the separate result in the testing guide.

MMIO accesses synchronize peripheral events at the end of the data-bus cycle.
[Interrupt timing](interrupts.md) describes register acceptance and CPU sampling.
The pre-reload overflow with divided clocks, simultaneous IRQ acknowledge and
assertion, and DMA bus arbitration still need broader hardware validation.
The current DMA engine transfers a burst
atomically and approximates its bus cost. Host tests establish deterministic
behavior for the tested interactions; they do not establish physical timing
for those remaining cases.
