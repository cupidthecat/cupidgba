# Wave-channel held output

Wave playback captures an unscaled four-bit nibble separately from the scaled
output. A volume write changes the active output immediately, using that held
nibble. Muting and then restoring volume retains the full nibble rather than
trying to recover it from an already attenuated value. Volume codes 1, 2 and 3
select full, half and quarter level. Codes 4 through 7 select three-quarter
level, rounded down after multiplying the nibble by three.

Changing volume leaves the pending wave-clock deadline and both waveform
banks intact. Byte writes to the volume field retain the length counter;
halfword writes update both the length and volume fields. These accesses
first observe the channel using its previous register values.

A stopped channel retains its held nibble and cached output while its volume
register can change. Triggering with the DAC enabled rescales the held nibble
using the current volume, before the next wave-clock deadline. Powering sound
off clears the held nibble along with the channel controls and retains wave RAM.
Wave RAM continues to use the existing cyclic shift representation.

In 64-sample mode, the selected bank supplies the first 32 samples and the
other bank supplies the next 32. Both banks rotate through the DAC tap at the
start of the selected bank. A complete 64-step cycle restores the waveform
bytes. In 32-sample mode, only the selected bank rotates. Bank-control writes
select the tap for the next wave step and retain the pending clock deadline.
The selected-first ordering follows the
[AGB Programming Manual](https://www.manuallib.com/download/2023-10-17/AGB%20Programming%20Manual.pdf),
page 93.

## Regression coverage

Three native groups cover 114,688 scenes across all 16 nibble values and eight
old/new volume codes, both register widths, bank and length modes, repeated
mute/unmute writes, stopped-channel triggers, four clock origins and stereo
mixer output. A byte-oriented rotation oracle checks 128 wave steps per
timeline, both complete bank lengths and the pending deadline after writes.

Three bank groups add 8,960 scenes and 622,592 sample observations. They cover
all 16 constant values independently in both banks, both bank and size settings,
byte/halfword loads and controls, nonuniform patterns, four clock origins,
complete cycles, CPU-window reads and bank changes between sample edges.
The volume timeline oracle also selects the programmed bank's DAC tap.

These checks qualify the selected held-output model. Trigger startup delay,
physical register acceptance, CPU-window clock phase and analog output calibration
remain open in [issue 1](https://github.com/cupidthecat/cupidgba/issues/1).
