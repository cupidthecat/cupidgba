# Noise-channel clock and held output

The noise channel stores a 15-bit polynomial register, a running divider
countdown and the time of its last observed clock. A frequency or ratio write
supplies the next divider reload and retains the pending countdown. Width
changes select the feedback positions used at the next polynomial step.

Shifts 14 and 15 stop the polynomial clock. The countdown and polynomial bit
remain stored while the envelope continues to control amplitude. Returning to a clocked shift resumes
the pending countdown without replaying the elapsed stopped time. The next
reload uses the current ratio and shift.

The stored polynomial uses complemented bits. Each step shifts the register
right and inserts its feedback into bit 14, and into bit 6 in short mode. The
shifted low bit supplies the held output level. An envelope update scales that
bit using the new volume, including when the previous amplitude was zero.

## Regression coverage

Five native groups cover 2,917,896 scenes. They exercise every 15-bit stored
state in both widths, volume boundaries, all clocked ratio/shift combinations,
byte and halfword writes, pending reloads, stopped shifts and resumption,
envelope changes, four clock origins and scheduled stereo mixer captures.

The checks qualify this counter and output-state model. Trigger startup and
retrigger-volume behavior, exact bus acceptance and pause/resume alignment on
physical hardware, and the output signal remain subject to the broader
qualification in [issue 1](https://github.com/cupidthecat/cupidgba/issues/1).
