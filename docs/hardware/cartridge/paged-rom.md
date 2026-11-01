# Paged ROM cartridges

A cartridge image larger than 32 MiB and no larger than 64 MiB selects the
paged controller when the first game-code byte at offset `ACh` is `M`.
Other oversized images are rejected. The complete read must still contain the
same controller identifier checked before allocation. The loader keeps the complete source
image in owned memory, separate from its 16 MiB CPU-visible view. Replacement
prepares both allocations before releasing the old cartridge; later mapping
does not depend on an open host file. See [image ownership](image-mapping.md).

## Window and reset

The controller updates an 8 KiB aperture at the beginning of the ROM view.
The remainder of the visible allocation starts at zero. Ordinary ROM mirrors
and wait-state selection still apply. Reset restores these two mappings:

- View `0000h..0FFFh` receives source `0000h..0FFFh`.
- View `1000h..1FFFh` receives source `0200h..11FFh`.

The source image itself is immutable. Reset leaves the command at zero and
source/destination/length at `0200h`, `1000h` and `1000h`. It preserves battery
storage. Replacing the image clears the controller and releases its old backing
allocation, including when switching back to an ordinary cartridge.

## Controller writes

The word-write port is `08800100h..088001FFh`, with corresponding ports at
`0A800100h` and `0C800100h`. Register selection repeats every 64 bytes:

| Offset | Meaning |
| --- | --- |
| `00h` | Command. `01h` and `11h` copy the selected source bytes into the window. |
| `04h` | Source byte offset, masked to 26 bits. |
| `08h` | Destination byte offset, masked to 23 bits before window validation. |
| `0Ch` | Nonzero page count, converted to a byte length in 512-byte units. |

A valid command copies one through fifteen pages. Destination and length must
be 512-byte aligned and the complete destination must fit within the 8 KiB
aperture. A sixteen-page command is rejected by the selected model; reset uses
two smaller copies. Source offsets need not be page aligned. Invalid windows
and unknown commands leave the visible bytes unchanged. Unknown commands still
update the command register. A zero count write preserves the previous length.

The supported halfword port is the first wait-state window, `08800100h`.
A halfword updates the low half of the selected stored register, preserving its
upper half; the address's `+2` alias also selects that low half. The count
conversion follows this merge. Byte writes do not change controller state.
These narrow-write and mirror rules describe the selected software behavior,
not a new electrical measurement. Reads still use the ROM view rather than
exposing the internal control registers.

Count conversion uses 32-bit arithmetic. A wrapped zero byte length leaves
the controller inactive until reset. A source beyond the supplied image
copies nothing. A source range crossing EOF copies its available prefix and
retains the unread destination tail. Both cases stay within allocated memory.

## Bus and instruction behavior

CPU and DMA stores reach the controller through the ordinary timed ROM write
path. The window changes at the completed destination write. Mapping does not
add an invented busy interval or alter CPU/DMA wait tables, release clocks or
completion IRQ stages. An active CPU ROM cache keeps pointing at the same
visible allocation. Future fetches see the new bytes; instructions already in
the CPU pipeline retain their fetched values.

## Verification and limits

Seven native groups cover 1,620 complete-window mappings across three ROM
mirrors, all valid destination/count combinations, both source halves and both
copy commands. They also cover register masks and aliases, invalid ranges,
short backing reads, ARM/Thumb stores, cached instruction fetches, retained
prefetched opcodes, DMA writes/IRQs across clock wrap, full-image loading, reset
and ordinary/paged replacement. Twenty-four injected failures preserve complete
memory, cartridge, scheduler and CPU-cache state with no old image, an ordinary
image or a paged image attached. Each new group has a twenty-second deadline;
existing image tests retain their original limits.

The owned end-to-end cartridge maps nineteen chunks from the upper 32 MiB,
uses DMA3 to copy all 76,800 bitmap bytes into VRAM, and draws a complete image.
The observer requires its guest-written iteration record, all 96 KiB of VRAM,
all 38,400 rendered pixels and exactly four presentations. Both firmware paths
use explicit guest entry, so this test does not qualify a commercial boot
header or the external BIOS boot sequence. It retains a twenty-second process
and thirty-second CTest limit.

A private independent component comparison retains all four control registers
and all 8,192 visible bytes after 14,694 writes/reset observations. Owned images
provide loader and execution coverage without redistributing cartridge data.
This does not establish full commercial movie playback, physical page-copy
latency, unobserved command meanings or every board revision. Those remain
under the broader cartridge work in issue 1.
