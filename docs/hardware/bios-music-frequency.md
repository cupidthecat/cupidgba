# BIOS music-frequency conversion

SWI 1F executes Thumb interpolation and ARM multiply-high instructions through
the ordinary CPU, bus and peripheral scheduler. R0 points to a wave header whose
32-bit base frequency is read at offset four. R1 supplies the coarse key, and
the low byte of R2 supplies its fractional adjustment.

The routine selects adjacent semitone factors, applies their octave shifts,
and interpolates using unsigned multiply-high arithmetic. A second multiply-high
operation scales the wave frequency. The two truncation stages retain the
firmware's integer approximation. Nonnegative keys above 178 select key 178
and adjustment 255. Fine-adjustment bits above the low byte do not affect an
ordinary key.

R0 returns the frequency, R1 returns the interpolated factor, and R3 returns
the shared firmware return address. The dispatcher preserves the caller's
other tested registers, flags, banked saved status and stack. The final
protected BIOS word is `E3A02004`. The 180 octave/semitone indices and 12
nearest Q31 semitone factors are generated with decimal arithmetic by
`tests/bios/generate_music_table.py`.

## Qualification

The local firmware comparison checks 917,504 generated ARM/Thumb calls:
every byte-valued key and adjustment, seven full-width base frequencies,
all registers, caller status, the protected BIOS word and elapsed clocks.
All observations match. This comparison uses the local external firmware
within the current CPU and bus; it is not a new physical timing capture.

Native numerical fixtures cover octave transitions, fractional interpolation,
upper-key clamping, adjustment aliases, full-width frequencies and system/IRQ
callers in both instruction modes. Source fixtures check unaligned reads,
EWRAM, IWRAM, all three cartridge wait banks, both sequential controls and
clock wrap. Their clocks include the actual wave read rather than a host
memory lookup.

A DMA transfer replaces the wave frequency during interpolation. Timer and
DMA interrupts execute before the call returns. Both firmware paths retain
matching frequencies, registers, status, BIOS protection, elapsed clocks,
timer counters, interrupt counts and pending requests. A second dispatch
phase requires genuine scheduler lateness and preserves the original event
timestamp across clock wrap.

Negative coarse-key registers and reads outside the intended pitch/header
data are unqualified. This service does not implement the remaining sound
driver, music-player or multiboot calls. Complete BIOS and system accuracy
remain tracked separately.
