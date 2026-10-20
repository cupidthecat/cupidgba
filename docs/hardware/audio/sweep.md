# Channel 1 frequency sweep

`SOUND1CNT_L` controls the channel 1 frequency sweep. A trigger copies the
programmed 11-bit frequency into a separate shadow register, reloads the sweep
divider and clears the history of subtract calculations. A nonzero shift also
checks a target frequency at trigger time without writing that target back.
An overflowing target mutes the channel while the trigger still reloads the
length and envelope state.

The frame sequencer clocks the sweep divider at 128 Hz. A programmed pace of
zero reloads the divider as eight and performs no periodic calculation. Other
paces calculate a target when the divider expires. A valid target updates the
channel and shadow frequencies only when the shift is nonzero; a second target
is then checked for overflow without being written back. With shift zero, an
add calculation can overflow, while a subtract calculation retains the
programmed frequency.

Each subtract calculation records negate use until the next trigger. Sweep
register writes retain this history. Selecting add mode after a subtract
calculation disables the channel, including when another subtract-mode write
occurred between the calculation and the mode change. An expiry with pace zero
does not create negate history.

Changing the frequency through `SOUND1CNT_X` leaves the sweep shadow unchanged.
Changing the sweep register retains the current divider and its trigger-time
enable latch. The next divider reload uses the newly programmed pace. A new
trigger refreshes the shadow, divider, enable latch and negate history.

## Regression coverage

Four native test groups cover 688,424 scenes: every 11-bit frequency and sweep
register combination at trigger and divider expiry, repeated register writes,
byte and halfword accesses, length reload after overflow, shadow preservation,
pace changes, power transitions and trigger resets. Scheduled stereo tests
compare a tone with zero-shift subtraction against an otherwise identical
channel 2 tone, checking every PCM capture and its clock across four clock
origins and a repeat.

These checks qualify sweep arithmetic and stored state within CupidGBA's
existing frame sequencer. They do not establish the physical delay between an
overflow calculation and channel disable, frequency changes within an
oscillator period, startup phase, or analog output. Those timing and signal
requirements remain open in [issue 1](https://github.com/cupidthecat/cupidgba/issues/1).
