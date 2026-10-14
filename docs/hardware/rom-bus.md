# Game Pak ROM reads

The three 32 MiB ROM windows use their selected wait states to access the same cartridge image. Where an ordinary ROM read has no image data, CupidGBA returns the low sixteen bits of the halfword address. For example, a halfword read at `0x0D082468` returns `0x1234`. This also applies when no cartridge is attached.

Byte reads select the corresponding byte from that halfword. Word reads combine two consecutive halfwords and retain the CPU's existing unaligned rotation. CPU loads and DMA transfers use the same fallback. This address-derived value is separate from the retained CPU value used for other unmapped regions.

An explicitly selected EEPROM still supplies its serial output through the existing `0x0D` access path. SRAM, flash and automatic save detection do not select EEPROM output. The fallback does not change EEPROM address decoding, cartridge mirroring, prefetch timing or GPIO behavior.

Four native regression groups cover a complete halfword-address period in all six ROM regions, both absent and attached images, image boundaries, ARM/Thumb loads, halfword and word DMA across address rollover, and each save selection. These are software checks; physical cartridge address counters and electrical timing remain separate qualification work.
