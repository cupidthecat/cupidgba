# Game Pak memory bus

## ROM reads and instruction fetches

The three 32 MiB ROM windows use their selected wait states to access the same
cartridge image. Where an ordinary ROM read has no mapped image data, CupidGBA
returns the low sixteen bits of the halfword address. A halfword at
`0x0D082468` therefore supplies `0x1234`. This also applies when no cartridge
is attached. See GBATEK's descriptions of
[unused Game Pak reads and memory mirrors](https://akkit.org/info/gbatek.htm#unpredictablethings).

Byte loads select a byte from that halfword. Word loads combine consecutive
halfwords and apply the CPU's unaligned rotation. ARM instruction fetches use
the aligned word without data-load rotation, including when the logical PC
retains bit 1. Thumb fetches use the aligned halfword. CPU loads, DMA reads and
ordinary instruction fetches share the address-derived ROM value.

Opcode lookup checks the mapped image extent before using its direct memory
view. A branch outside that extent and a sequential fetch crossing its end
both receive the undriven address value. Crossing into the next 32 MiB ROM
window selects that window's image address and wait states. The loader's
existing padding and board-specific mapped views remain part of
[image mapping](cartridge/image-mapping.md).

GPIO and card-reader opcode samples retain their existing device callback and
prefetch ownership. Image replacement rebinds the opcode view. The ROM fallback
reads no EEPROM command bits; selected EEPROM data accesses continue through
their existing serial path. EEPROM instruction-fetch signaling and physical
cartridge bus transitions remain unqualified.

## Backup wait states

`WAITCNT` bits 0 and 1 select the same SRAM wait count throughout the backup
windows at `0x0E000000` and `0x0F000000`. Their reset value is zero, so both
windows start with four wait clocks. Rewriting zero preserves that timing.
The complete byte-bus beat also includes its access clock:

| SRAM selector | Wait clocks | Bus clocks per access |
| --- | ---: | ---: |
| 0 | 4 | 5 |
| 1 | 3 | 4 |
| 2 | 2 | 3 |
| 3 | 8 | 9 |

Byte, halfword and word CPU backup accesses use the same external byte-bus
duration; their existing value expansion and byte selection still apply.
Instruction fetch and load-internal clocks are separate from this table.
See the [WAITCNT fields](https://akkit.org/info/gbatek.htm#gbasystemcontrol)
and the memory-mirror description linked above.

## Regression coverage

The four `gba.rom-bus-*` groups cover a complete halfword-address period in all
six ROM regions, absent and attached images, ARM/Thumb data loads, DMA address
rollover and save selection. The `gba.rom-fetch-*` groups cover opcode refills,
executed branches, image ends, wait-window transitions, image mapping and
replacement ownership. The `gba.backup-waits-*` groups cover reset timing,
CPU accesses and the existing DMA model's backup-window timing. Each group
retains a ten-second deadline. See [memory tests](../development/testing/memory.md)
for commands and the scope of these software checks.
