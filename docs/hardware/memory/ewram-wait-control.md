# Internal memory timing

The control port at 04000800h repeats in every 64 KiB I/O bank. Its normal
mapping reads 0D000020h after reset. High-halfword bits 8-15 read back written
values; bits 0-7 are unused. The lower nibble of that writable byte selects
the EWRAM timing. The upper nibble is retained without assigning unknown effects.

Timing values 0 through 14 select 15 through 1 wait states. Byte and halfword
accesses take one bus cycle plus those waits; words take two transfers. The
reset value 13 selects three clocks per byte or halfword and six per word.
Guest byte, halfword and word writes update the shared timing tables and an
active EWRAM opcode cache. Queued instructions remain in the CPU pipeline.
Later fetches, data accesses and DMA use the updated timing.

## Limits

The low control halfword returns the normal enabled-RAM value; its
disable/remapping writes remain unsupported. Timing value 15 reads back as
written while the tables retain their last valid timing. This selected policy
does not model physical RAM failure. RAM disable/remapping, model-specific
timing failures and full hardware qualification remain open.

## Checks

Owned regressions cover mirrored ports, writable and reserved lanes, ARM and
Thumb stores, live EWRAM instruction execution, read/write timing, both DMA
directions, decoder boundaries and reset/instance ownership. These are software
checks against documented controls, not new hardware captures.
