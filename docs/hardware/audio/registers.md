# Sound register access

CupidGBA dispatches PSG byte writes to the addressed field. Writing an envelope
byte preserves the running length counter; writing a frequency byte preserves
the envelope and does not repeat a restart bit from the untouched byte. An
explicit write to the trigger field still restarts the channel. Halfword writes
update both fields at the same bus observation time.

Accepted accesses capture pending mixer deadlines and advance the affected
channel using its old register values before applying the write. Sound-control
bytes retain the qualified routing, volume and FIFO reset masks described in
[I/O registers](../io-registers.md). Reserved register bytes have no write side
effects.

Powered FIFO byte and halfword stores append a word while replacing only the
addressed lanes of its next storage slot. Word requests append once at the
existing bus deadline. Queue storage, staged bytes and the held output retain
separate state; see [FIFO access and power](fifo-power.md).

Wave RAM holds two 16-byte banks. The CPU window at `0x04000090` through
`0x0400009F` accesses the bank opposite the programmed playback bank. Byte,
halfword and word reads use the actual waveform storage. A byte write retains
the adjacent byte from that bank. Playback shifts complete four-bit samples
cyclically; the first byte's high nibble precedes its low nibble. Complete
32-step and 64-step cycles preserve the programmed data.

Powering sound off mutes PSG and Direct Sound. PSG mode registers reset while
wave RAM remains stored. PSG and `SOUNDCNT_L` writes are ignored while powered
off; the CPU wave window remains accessible in bank 1. These bank and power
rules follow the [AGB Programming Manual](https://www.manuallib.com/download/2023-10-17/AGB%20Programming%20Manual.pdf),
pages 93, 96 and 101.

FIFO stores while master sound is off are ignored. Disabling sound discards
queued words without canceling an accepted DMA transfer. See
[FIFO power](fifo-power.md) for access-width, transfer and qualification details.

Native regressions cover both banks and every access width, ARM/Thumb byte
stores, running counters and envelopes, explicit triggers, ignored bytes,
accesses during playback, complete wave cycles and power transitions. The
unchanged local register-read suite remains required with either BIOS path.
The signal cartridge also checks the complete programmed wave pattern.

Channel 1 sweep arithmetic, negate history and divider writes are described in
[Frequency sweep](sweep.md).
Both square channels retain their pending duty edge across frequency writes;
see [Square-channel duty timers](oscillator.md).
The noise channel retains its pending countdown and scales the shifted low bit;
see [Noise-channel clock and held output](noise.md).
Wave volume writes rescale the retained unscaled nibble without changing the
clock deadline or waveform banks; see [Wave-channel held output](wave.md).

The wave trigger startup delay is retained and remains unqualified. The
programmed first bank and CPU window phase during active 64-step playback,
frequency changes inside an oscillator period, retrigger corruption, FIFO
partial writes and reset pipeline behavior, and `SOUNDCNT_H` power transitions
require further hardware evidence.
These limits remain in [issue 1](https://github.com/cupidthecat/cupidgba/issues/1).
