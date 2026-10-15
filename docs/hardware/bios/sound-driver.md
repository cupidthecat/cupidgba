# BIOS sound driver

SWIs 1Ah through 1Eh, 28h and 29h execute replacement firmware through the
normal CPU and memory bus. Initialization, software mixing, work-area stores,
FIFO DMA control writes, PCM clearing and timer setup advance ordinary
peripheral clocks. Timers, DMA and unmasked IRQ handlers can run during a call.

## Initialization

SoundDriverInit attaches the caller's work area through the word at
`03007FF0h`. It stops DMA1/2, enables sound, configures Direct Sound routing
and selects SOUNDBIAS resolution 1 while retaining the lower accepted bias
bits. It programs DMA1/2 from the two PCM buffers to FIFO A/B and clears
exactly `0FB0h` work-area bytes through CpuSet. Bytes after that area survive.

Defaults select eight channels, master volume 15 and playback selector 4:
224 samples per frame, nominal rate 13,379, resampling reciprocal 627 and a
seven-frame ring period. Timer 0 receives reload `FB1Ah`. Initialization
uses the same scanline synchronization as a rate change and publishes the
ready identifier only after setup finishes.

The default extension hooks return without changing state. The reserved
music jump table and note helper are initialized to live functions; see
[music-player services](music-player.md). Detached library hooks retain their
return functions until the caller installs extensions. Address words
are implementation details, so they are checked for bounds and supported
callability separately from complete PCM and channel-state comparisons.

## Work-area ownership and mode

The ready identifier is `68736D53h`. Mode, VSync and channel clear return
without changing the work area when that identifier does not match. Mode and
clear increment it while accessing the area and restore it before returning.
VSyncOff also accepts an area already marked busy; its nested increment and
decrement preserve the original identifier.

SoundDriverMode stores nonzero channel-count and master-volume nibbles. A
channel-count change clears all twelve 64-byte channel status fields, beginning
at offset `50h`, and preserves their remaining payload. A nonzero low byte
updates reverb with its lower seven bits, including values without bit 7 set.
The mixer consumes these fields on subsequent calls. The resolution selector
updates the upper two SOUNDBIAS bits through a byte store while preserving
the remaining accepted bits.

A nonzero playback selector configures the documented twelve rates. Their
sample counts per 280,896-clock frame are 96, 132, 176, 224, 264, 304, 352,
448, 528, 608, 672 and 704. The service computes the ring period, nominal rate,
resampling divisor and timer reload with integer division. Division enters
the existing ARM routine through a direct Thumb-to-ARM bridge.

Rate changes disable sound DMA, clear the PCM buffers, stop timer 0 and program
its reload. DMA is then enabled before the firmware leaves scanline 159 and
waits for its next occurrence. Timer 0 starts at that modeled scanline. This
is a real VCOUNT polling loop, so elapsed time depends on the entry phase;
there is no host-side delay or skipped peripheral interval. Reserved rate
selectors and malformed work areas remain unqualified.

## Software mixing

SoundDriverMain requires the ready identifier and marks the area busy until
it returns. It calls the optional frame hook at offset `20h` with the argument
at `24h`, then calls the extension hook at `28h` with the work-area pointer.
Both ARM and Thumb hooks execute through the CPU while the identifier is busy.

The frame counter and sample count select a segment in each 1,584-byte PCM
ring. With reverb disabled, the service clears that segment. Reverb combines
the signed bytes from both channels in the current and following segments,
applies its integer coefficient and rounding, and writes both outputs.

Each selected 64-byte channel advances its attack, decay, sustain, release or
echo state. Start requests initialize the wave cursor, remaining length and
fractional phase; simultaneous start/stop requests stop the channel. Envelope,
master volume and left/right volume determine the stored gains. A zero gain
still advances the sample position.

Fixed-rate channels consume signed bytes directly. Fractional channels track
phase against the nominal rate, advance the cursor with loop bounds and use
integer linear interpolation with the stored reciprocal. Looping restarts at
the wave header's loop point. A one-shot ending during a frame clears its
status without overwriting the previously saved cursor and remaining length.
Mixing adds into byte buffers with wrapping arithmetic. There is no host
floating-point mixer or saturating replacement for these firmware operations.

## Frame cadence and channel clearing

SoundDriverVSync decrements the byte counter at offset 4. An old value of 0
or 1 reloads it from offset `0Bh`, stops DMA1/2 and writes `B600h` to their
control registers. Other values retain the decremented counter without a DMA
restart. The read, counter store and four DMA stores occur at separate bus
strobes.

SoundDriverVSyncOff stops DMA1/2, clears that counter and fills exactly 3,168
PCM bytes beginning at offset `350h` through CpuSet. Header, channel payload
and bytes beyond the buffers remain intact. VSyncOn writes `B600h` to DMA1/2
without requiring a work-area identifier or changing the buffers.

SoundChannelClear clears the twelve channel status bytes. When an extension
block is attached at offset `1Ch`, it calls the function at offset `2Ch` four
times with channel numbers 1 through 4 and successive 64-byte blocks in R5.
The access identifier remains busy during each call. A final status byte is
cleared in the following extension block. Both ARM and Thumb callbacks run
through normal instructions.

## Qualification

Nineteen native groups cover initialization, mode fields, all frame-counter
values, identifier guards, RAM regions, ARM/Thumb callers and callbacks,
System/IRQ banks, all twelve rates, timer/DMA IRQs, visible partial PCM clearing
and clock wrap. Supplying `CUPID_TEST_BIOS` repeats these checks with a local
external firmware input. All eleven initialization/mixer groups fail against
the preceding control-only core, whose eight control groups still pass.
No firmware or cartridge input is distributed.

Owned synthetic waves cover empty, fixed and fractional channels, short waves,
high cursor advancement, loop endpoints, zero attack, the envelope stages,
release/echo, start/stop, reverb and twelve simultaneous voices. The fixtures
check 84 frame captures and another 36 across all twelve playback rates.
The mixer rate corpus uses three four-selector batches, retaining every case,
assertion and ten-second deadline.
Each signature includes every byte in the 4 KiB prepared work-area image
except the six reserved internal-address words at `28h` through `3Fh`.
Separate IWRAM fixtures verify complete state and elapsed clocks across clock
wrap and above the 32-bit clock range. A real timer IRQ calls VSync while the
mixer retains ownership.

Two twelve-frame playback fixtures keep timer 0 and both FIFO DMA channels
active during mixing. They check all 26,336 emitted stereo samples and their
timestamps through deterministic signatures, retained work-area state, DMA
source advancement and frame-counter wrap. The output queue is consumed each
frame, and the tests require zero overruns. These checks exercise the modeled
digital audio path; they do not measure physical audio output.

The rate fixtures explicitly align both video engines to scanline zero before
calling. The ordinary built-in fast-start and external reset paths begin at
different video phases, so their raw wait durations cannot be compared without
that setup. Controlled IWRAM callers with an EWRAM work area match 107 clocks
for a DMA-restarting VSync, 83 without restart, 245 for channel clear without
an extension, 9,685 for VSyncOff and 60 for VSyncOn. These are the tested
software paths and initial states, not physical cartridge measurements.

Music-player lifecycle and exported helpers have separate
[qualification coverage](music-player.md). Invalid pointers, malformed
wave/work-area state, extended library variants and physical audio output
remain unqualified. Full BIOS and audio acceptance remain open.
