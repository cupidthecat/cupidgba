# EEPROM address decoding

EEPROM commands carry a two-bit prefix, a six-bit or fourteen-bit block
address, and a stop bit. Writes also carry 64 data bits. Read responses contain
four dummy bits followed by 64 data bits. The data bits are MSB first.

A 512-byte device decodes six block bits. An 8-KiB device decodes ten block
bits while consuming all fourteen address bits on the wire. The unused high
bits do not select storage beyond the device. Each block holds eight bytes;
the last block ends at the last byte of the selected capacity.

When capacity has not been explicitly selected, a complete 17-bit read request
or 81-bit write command selects 8 KiB at the second prefix bit. This selection
uses the DMA transfer length, including for block zero. Expansion preserves
the first 512 bytes and initializes the added bytes to `FF`. Explicit save-type
choices and capacities loaded from compatible save files remain available.
Six-bit commands retain an inferred 512-byte capacity.

The command returns to idle after the last data bit of a read. All response
bits are zero or one. Address decoding does not change the existing
[cartridge port selection](image-mapping.md), DMA phases, or
[save-file persistence](save-files.md).

## Regression coverage

Five native groups cover 16,448 complete write/read address cases, 16,384
read-only address cases, 2,176 capacity selections, 36 complete DMA3 transfers,
and three save/reset/load cases. They check every fourteen-bit address,
both capacities, preserved surrounding bytes, all response bits, return to
idle, small and large cartridge mappings, and both scheduler clock boundaries.
The file checks compare the complete saved image and retain capacity through
reset and reload. Each group has a ten-second deadline.
Cancelled-write cases also check that a resumed transfer cannot access past
either save capacity; they do not establish electrical cancellation behavior.

These checks qualify CupidGBA's address and capacity model. Physical alias
captures, programming latency, busy polling, malformed transfer lengths,
interrupted commands, and electrical wire behavior remain unqualified. The
current write and ready-bit timing still needs separate work under
[issue 1](https://github.com/cupidthecat/cupidgba/issues/1).
