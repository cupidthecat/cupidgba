# Internal memory mapping

The 32-bit control port at 04000800h repeats every 64 KiB throughout the I/O
region. Reset selects 0D000020h. The low halfword retains bits 0–3 and 5;
reserved bits read zero. Byte writes to its unused high byte have no effect.
A word write updates both mapping and [EWRAM timing](ewram-wait-control.md).

## Address decoder

Bit 0 swaps the low two 32-MiB address windows. Bit 5 selects external RAM;
clearing it selects the 32-KiB internal RAM in the external RAM window too.
The resulting map is:

| Low control bits 5, 0 | 00000000h | 01000000h | 02000000h | 03000000h |
| --- | --- | --- | --- | --- |
| 1, 0 | BIOS | Unmapped | External RAM | Internal RAM |
| 0, 0 | BIOS | Unmapped | Internal RAM | Internal RAM |
| 1, 1 | External RAM | Internal RAM | BIOS | Unmapped |
| 0, 1 | Internal RAM | Internal RAM | BIOS | Unmapped |

Each column describes a 16-MiB window. RAM mirrors its physical capacity
within that window. BIOS data is present only in its first 16 KiB; the rest
uses the existing open-bus path. I/O, display memory, cartridge ROM and backup
storage keep their normal addresses.

Remapping does not copy or clear RAM. Data remains in its physical buffer
while another device occupies its previous address. CPU loads and stores,
block transfers, opcode views and DMA all use the same decoder. Internal-RAM
input-bank feedback follows the selected device, including its aliases.

BIOS read protection remains in force after the swap. Code running outside
the selected BIOS device reads its retained protection latch instead of the
underlying bytes. DMA reads from the BIOS and unmapped low window retain the
channel's existing read latch. Writes cannot modify BIOS storage.

## Changing the map during execution

An accepted control write updates the direct opcode view and its timing
without performing a branch. Instructions already fetched remain in the
pipeline; subsequent requests use the new map. The change retains the last
jump record and does not reset the cartridge prefetch stream.

Bus waits follow the selected device: internal RAM uses its 32-bit port;
external RAM uses the programmed halfword waits. DMA still has separate read,
write and release phases. A mapping write does not bypass CPU bus ownership
or add an IRQ delay.

## Evidence and limits

The decoder follows the internal-memory-control description in
[GBATEK](https://problemkaputt.de/gbatek.htm). The selected port timing and
DMA/protection composition are a software model, not new hardware measurements.
Tests establish that these paths agree with the documented map and retain the
existing bus rules; they do not independently measure the decoder's electrical
propagation or every same-edge mapping change. Unmapped instruction fetches
retain the existing invalid-region handler; these checks do not qualify that
handler as a physical open-bus opcode source.

Bits 1–3 are retained without adding undocumented GBA effects or another
console mode. Invalid EWRAM timing settings keep the existing last-valid-wait
policy. Physical RAM failure, model-specific timing limits and wider input
feedback qualification remain open under
[issue 1](https://github.com/cupidthecat/cupidgba/issues/1).

See [memory regression tests](../../development/testing/memory.md) for the
register, storage, protection, timing, pipeline, DMA and feedback checks.
