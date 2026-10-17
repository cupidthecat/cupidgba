# Audio capture and host output

The mixer captures PSG and Direct Sound at the same deadline. The selected
SOUNDBIAS resolution gives a stable interval of 512, 256, 128 or 64 CPU clocks,
corresponding to 32.768, 65.536, 131.072 or 262.144 kHz. The rates are specified
in the [AGB Programming Manual](https://www.manuallib.com/download/2023-10-17/AGB%20Programming%20Manual.pdf),
page 104.

Register writes, wave reads, frame-sequencer changes and FIFO timer updates
flush completed captures before changing their state. Each captured frame is
immutable. FIFO bytes hold between timer overflows. At a coincident timer and
capture deadline, the implementation captures the previous hold value before
the overflow replaces it; the existing boundary regression retains this order.

A sampling-resolution write retains the already queued deadline. Subsequent
captures align to the selected divider. Each frame records the elapsed clocks
since the preceding capture, so a partially completed interval is retained.
Late dispatch advances the same chronological deadlines as punctual dispatch.
The reset epoch preserves divider phase across the scheduler's clock wrap.

SDL2 linearly interpolates along those captured durations at the negotiated
host rate. The frontend holds the [audio device lock](https://wiki.libsdl.org/SDL2/SDL_LockAudioDevice)
while producing each frame; the callback uses SDL2's corresponding lock while
consuming the queue. Rendering and frame pacing occur after releasing it.
Queued frames retain their timing when a later register write changes
the current resolution. The consumer advances only through available frames.
On underrun it retains the final source anchor and fades toward zero. On output
queue overflow, the producer drops the oldest queued frame and the consumer
resets its interpolation anchor. Emulated FIFO contents are unaffected. An
optional sample sink receives every capture even when the host queue overflows.

Native tests check all four rates, pending writes and resolution changes,
punctual versus late dispatch, clock wrap, power-off muting, full capture during
queue overflow, variable-rate interpolation, ring wrap, callback partitioning
and recovery from underrun. A live SDL2 dummy device also consumes output while
the producer runs frames under the device lock, including a separate
ThreadSanitizer build. The unchanged signal cartridge checks ten selections:
tones, stereo routing, frequency sequences, a volume ramp, noise, the wave
pattern, sampling modes, signed FIFO levels and silence. Complete capture
timestamps are required. Periodic signal checks allow an unknown initial phase.

The cartridge results are source qualification, not a new hardware trace.
FIFO signal checks qualify signed table levels, routing and hold timing; they
do not prove every DMA or FIFO edge. Coincident timer/mixer phase, the first PWM
edge after a resolution change, fractional output precision and the analog
output path still require qualification. The [digital output conversion](digital-output.md)
documents bias, saturation and fixed-midpoint PCM coverage. Complete audio accuracy
remains open in [issue 1](https://github.com/cupidthecat/cupidgba/issues/1).
