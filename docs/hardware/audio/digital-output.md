# Digital mixer output

The mixer adds the programmed `SOUNDBIAS` level to each stereo channel, clips
the result to the ten-bit DAC range `0..1023`, then converts to signed PCM
around the fixed midpoint `512`. The existing host gain follows that conversion.
At maximum gain the output is bounded by `-24576..24528`. Programming a different
bias shifts silence as well as the positive and negative clipping thresholds.
Subtracting the programmed bias after clipping would erase that shift and could
wrap a valid extreme level into the opposite signed PCM polarity.

The default bias `0200h` keeps the previous output exactly. For example, two
full-volume FIFO samples of `+127` produce `24528`; two samples of `-128`
produce `-24576`. With bias zero, the positive pair instead produces `24192`.
With bias `03FEh`, the negative pair still clips to `-24576`. Silence at these
two bias extremes produces `-24576` and `24480`, respectively. Master sound
disable continues to mute the captured signal.

Bias writes use the existing [capture chronology](timing.md): completed samples
keep their old state, the queued deadline remains in place, and later captures
use the new level and divider. Byte writes update their addressed half of the
register; halfword and word accesses reach the same mixer state. Register masks,
FIFO request timing and frontend interpolation are unchanged.

Native regressions cover all 512 even bias levels against all 256 signed FIFO
values, monotonicity, clipping bounds, eight fixed output vectors, every stereo
routing combination, both FIFO gains, six host gains, three register widths and
power transitions. Paired punctual and late scheduler runs cover the clocks
before, at and after a bias/divider write across four clock epochs. Timed ARM
and Thumb firmware calls also ramp bias in both directions at all four sample
rates; the external BIOS repeats these checks when supplied locally.

The [AGB Programming Manual, pages 104-105](https://www.manuallib.com/download/2023-10-17/AGB%20Programming%20Manual.pdf#page=104)
describes the PWM bias and its default setting. CupidGBA selects this fixed
digital midpoint after comparing the source implementations and reproducing the
former PCM overflow. This qualifies a deterministic digital conversion; analog
filtering, physical output calibration and PWM precision still need separate
evidence under [issue 1](https://github.com/cupidthecat/cupidgba/issues/1).
