# BIOS sound-driver controls

SWIs 1Bh, 1Dh, 1Eh, 28h and 29h execute replacement firmware through the
normal CPU and memory bus. Their work-area stores, FIFO DMA control writes,
PCM clearing and timer setup advance ordinary peripheral clocks. Timers and
DMA can run while a service is executing.

These controls expect a prepared sound work area referenced by the word at
`03007FF0h`. The built-in SoundDriverInit and SoundDriverMain services remain
unimplemented. Initialization, software mixing, envelopes, looping and reverb
processing are still required in issue #66. The controls alone do not provide
a complete BIOS sound driver.

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
These are firmware state operations; the mixer that consumes those fields is
not implemented yet. The resolution selector updates the upper two SOUNDBIAS
bits through a byte store while preserving the remaining accepted bits.

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

Eight native groups cover mode fields, all frame-counter values, identifier
guards, complete retained work-area images, RAM regions, ARM/Thumb callers and
callbacks, System/IRQ banks, the twelve rates, timer/DMA IRQs, visible partial
PCM clearing and clock wrap. Supplying `CUPID_TEST_BIOS` repeats these checks
with a local external firmware input. All groups fail against the preceding
built-in core; no firmware or cartridge input is distributed.

The rate fixtures explicitly align both video engines to scanline zero before
calling. The ordinary built-in fast-start and external reset paths begin at
different video phases, so their raw wait durations cannot be compared without
that setup. Controlled IWRAM callers with an EWRAM work area match 107 clocks
for a DMA-restarting VSync, 83 without restart, 245 for channel clear without
an extension, 9,685 for VSyncOff and 60 for VSyncOn. These are the tested
software paths and initial states, not physical cartridge measurements.

Undocumented music-player helpers, jump-table completeness, invalid pointers,
extended library variants, complete driver playback and physical audio output
remain outside this increment. Full BIOS and audio acceptance remain open.
