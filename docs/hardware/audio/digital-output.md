# Digital mixer output

The mixer adds the programmed `SOUNDBIAS` level to each stereo channel, clips
the result to the ten-bit range `0..1023`, then reduces it to the selected PWM
precision. It converts that level to signed PCM around the fixed midpoint
`512`. The existing host gain follows that conversion. At the default divider
and maximum gain the output is bounded by `-24576..24480`. Programming a different
bias shifts silence as well as the positive and negative clipping thresholds.
Subtracting the programmed bias after clipping would erase that shift and could
wrap a valid extreme level into the opposite signed PCM polarity.

At the default bias `0200h`, two
full-volume FIFO samples of `+127` produce `24480`; two samples of `-128`
produce `-24576`. With bias zero, the positive pair instead produces `24192`.
With bias `03FEh`, the negative pair still clips to `-24576`. Silence at these
two bias extremes produces `-24576` and `24480`, respectively. Master sound
disable continues to mute the captured signal.

Bias writes use the existing [capture chronology](timing.md): completed samples
keep their old state, the queued deadline remains in place, and later captures
use the new level and divider. Byte writes update their addressed half of the
register; halfword and word accesses reach the same mixer state. FIFO request
timing and frontend interpolation retain their existing behavior.

## Writable bias fields

Only bits 1–9 of the bias level and bits 14–15 of the divider are writable.
Bit zero and bits 10–13 read as zero. Every accepted byte, halfword or word
write uses mask `C3FEh` before updating register storage, readback or the
digital mixer. Bias remains writable while master sound is disabled. A word
read has a zero upper halfword.

Ignoring bit zero also prevents it from shifting captured PCM. For example,
writing `03FFh` selects the same level as `03FEh`; muted-channel silence at
maximum host gain is `24480` when master sound is enabled. Disabled master
sound still produces zero. The divider selects both the capture interval and
the precision described below.

## PWM precision

The [AGB Programming Manual, page 104](https://www.manuallib.com/download/2023-10-17/AGB%20Programming%20Manual.pdf#page=104)
specifies four amplitude precisions. Each PWM period contains a whole number of
console clocks. CupidGBA converts the clipped ten-bit input into that integer
pulse count, retaining its original scale for signed PCM and host gain.

| Divider | Capture clocks | Precision | Ten-bit level step | Maximum PCM at full gain |
| --- | ---: | ---: | ---: | ---: |
| 0 | 512 | 9 bits | 2 | 24480 |
| 1 | 256 | 8 bits | 4 | 24384 |
| 2 | 128 | 7 bits | 8 | 24192 |
| 3 | 64 | 6 bits | 16 | 23808 |

Conversion discards the low 1, 2, 3 or 4 bits after adding every routed channel
and the bias and clipping their combined level. Quantizing each channel
separately would lose contributions that combine into a complete pulse.
Applying host gain before this step would make the emulated precision depend
on frontend volume. The signed lower bound remains `-24576` at full gain.

For example, clipped level `1023` becomes `1022` at the default divider and
`1008` at divider 3. Levels `511` and `512` become `496` and `512` at divider 3;
the conversion floors the unsigned level before subtracting the midpoint.
This follows the documented digital PWM conversion. It models neither output
pin edges nor analog filtering or calibration. Physical behavior during a
divider change remains unmeasured; the existing capture deadlines are retained.

DMA destination writes apply the same register decode. The transferred data
and DMA latch retain their complete raw bits; register masks affect the
destination state. A later CPU read publishes the masked register value before
an eligible DMA takes bus ownership. Reserved bits therefore drive zero
without replacing the independent retained IWRAM lanes.

Native regressions cover all 512 even bias levels against all 256 signed FIFO
values, monotonicity, clipping bounds, eight fixed output vectors, every stereo
routing combination, both FIFO gains, six host gains, three register widths and
power transitions. Paired punctual and late scheduler runs cover the clocks
before, at and after a bias/divider write across four clock epochs. Timed ARM
and Thumb firmware calls also ramp bias in both directions at all four sample
rates; the external BIOS repeats these checks when supplied locally.

Six `gba.bias-control-*` groups cover 655,360 register-access cases, 2,304
ARM/Thumb guest stores and loads, 768 DMA destination cases, 4,608 CPU-input
handovers, 131,072 canonical/ignored-bit PCM pairs and 768 chronology pairs.
They include every 16-bit register stimulus, both master-power states, both
byte-write orders, four DMA channels, unaligned loads and stores, all four
divider values and clock wrap. The PCM and chronology pairs require identical
samples and deadlines for ignored-bit and canonical writes. All six groups
reject the preceding implementation. The sound-driver mode test retains its
odd write stimulus and requires the masked bias field with either BIOS path.

The [AGB Programming Manual, pages 104-105](https://www.manuallib.com/download/2023-10-17/AGB%20Programming%20Manual.pdf#page=104)
describes the PWM bias and its default setting. CupidGBA selects this fixed
digital midpoint after comparing the source implementations and reproducing the
former PCM overflow. This qualifies a deterministic digital conversion; analog
filtering, physical output calibration and PWM pin transitions still need separate
evidence under [issue 1](https://github.com/cupidthecat/cupidgba/issues/1).

Eight `gba.pwm-precision-*` groups check all 1024 clipped input codes in every
mode, using 1,048,576 bias/FIFO/PSG combinations, 55,296 stereo/gain/routing
cases, 192 punctual/late divider pairs, 288 timed ARM/Thumb stores and 256 DMA
scenes. The sink, current sample and ring buffer must agree. CPU and DMA writes
retain register masks, raw DMA payloads, completion interrupts and queued
capture clocks. All eight groups reject the preceding ten-bit output model.
Existing DAC levels, routing, numerical anchors and firmware ramps use the
same pulse-count expectations; their instructions, stimuli and guards remain
intact. These checks qualify the selected digital conversion and its covered
interactions, while FIFO request timing and the broader audio scope remain open.
