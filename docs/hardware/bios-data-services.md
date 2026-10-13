# BIOS data services

CupidGBA executes SWIs 10 through 19 in its replacement firmware. Unpacking,
decompression and bias changes use ordinary CPU instructions and memory
accesses. They no longer finish in an untimed C helper. A peripheral event can
occur between source reads, output writes or iterations, and IRQs and DMA can
interrupt execution before the caller resumes.

| SWI | Service | Output access |
| --- | --- | --- |
| 10 | BitUnPack | Words |
| 11 / 12 | LZ77 | Bytes / halfwords |
| 13 | Huffman | Words |
| 14 / 15 | Run length | Bytes / halfwords |
| 16 / 17 | 8-bit differential filter | Bytes / halfwords |
| 18 | 16-bit differential filter | Halfwords |
| 19 | SoundBias | Timed SOUNDBIAS halfword writes |

Use a destination that supports the selected write width. Word output needs
word alignment; halfword output needs halfword alignment. The unpack descriptor
selects 1-, 2-, 4- or 8-bit source units and destination widths through 32 bits.
Its offset applies to nonzero units, with an optional flag to include zero
units. Huffman tests use 4- and 8-bit leaves. Halfword LZ copies require a
distance of at least two bytes: the pending byte has not yet reached memory.
See the [BIOS data formats](https://problemkaputt.de/gbatek-bios-decompression-functions.htm).

The halfword routines buffer bytes until a pair is complete. An odd final byte
does not cause an extra bus write. Word unpacking similarly leaves a partial
word uncommitted. These tail observations are tested separately from the
documented complete-output formats. LZ copies read from the advancing
destination, so overlapping copies observe earlier writes. Huffman assembles
high-bit leaf bytes using the CPU's unsigned word operations.

The dispatcher preserves R2, R4 through R14 and the caller's CPSR. R0 and R1
advance according to the reads and committed output. R3 depends on the
service: a literal-only byte LZ call preserves it; the byte routine leaves zero
after a completed copy; Huffman leaves its final packed word; the Thumb byte/pair filters and
run-length calls leave the dispatch return address. Diff16 leaves the last
delta, or the source guard address `0x0BA4` if no later delta is loaded. The
replacement image preserves that observable guard address. Native checks also
cover zero lengths and protected low source addresses.

SoundBias steps the low bias field by two toward zero for R0=0, or toward
`0x0200` for nonzero R0. An upward request above `0x0200` leaves the level
unchanged. The sampling-resolution bits survive every write. R1 is not a
delay argument on GBA; the routine returns the final level there. Each write
is followed by instruction execution before the next write. The target and
fixed-delay interface are described in the
[BIOS bias interface](https://problemkaputt.de/gbatek-bios-sound-functions.htm).

## Qualification

The native corpus has 2,043 data-service observations with independent expected
bytes, source preservation, output guards, register state and a running timer.
It covers ARM and Thumb callers; EWRAM, IWRAM and cartridge sources; two
cartridge wait-state settings; and EWRAM, IWRAM and VRAM destinations where the
write width permits them. Inputs include unpack widths and offset flags, LZ
distances through 4,096 with both byte phases, multiple flag groups and overlap,
simple/deeper Huffman trees across stream words, maximum run descriptors, and
differential wraparound. Overflowing unpack offsets and incomplete output are
additional observed edge cases, not expanded format guarantees.

With `CUPID_TEST_BIOS` configured, every case also runs against the local
external BIOS. It must produce the same complete output, caller registers,
timer value and elapsed clocks. Nine small EWRAM-to-IWRAM fixtures have fixed
ARM call durations of 445, 258, 296, 521, 218, 278, 206, 260 and 146 clocks for
SWIs 10 through 18 respectively. They use the fixture data in
`tests/bios/test_data_services.c`; these are not constant service costs.

The separate 98-case bias corpus checks levels from zero through `0x03FE`, both
directions, all four sampling-resolution settings and both caller modes. It
samples each register transition at scheduled clock boundaries. A default
`0x0200` downward call takes 15,689 clocks through exception return. Two cases
also require a DMA transfer and its IRQ plus periodic timer IRQs during the
ramp, with preserved data guards and all requests acknowledged. The external
comparison checks the complete transition chronology as well as the call's
duration.

These tests establish the listed inputs on the current CPU, bus and peripheral
model. They do not constitute new physical-hardware captures or complete BIOS
qualification. Truncated streams, invalid Huffman trees, unsupported unit
widths and arbitrary misalignment have no general acceptance claim. Affine,
music-driver, multiboot and other unfinished services remain separate work.
