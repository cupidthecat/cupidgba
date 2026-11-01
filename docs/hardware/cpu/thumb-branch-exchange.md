# Thumb branch exchange

CupidGBA decodes the full `4700h` through `47FFh` register-branch class as BX.
Bits 6 through 3 select the source register. The unused destination bits,
including H1 at bit 7, do not select a link operation. The existing BX path
selects ARM or Thumb from the operand's low bit and refills the target pipeline.
It preserves LR, the caller's privilege bank, saved status and condition flags.

ARM DDI 0029E, section 5.5.3, describes H1=1 as undefined. Accepting this
form is a selected GBA compatibility behavior, not an ARMv5 BLX implementation
or a general architectural guarantee for reserved encodings.

## Regression scope

Four `arm.thumb-exchange-*` groups check 28,160 canonical/candidate pairs:

- All 256 encodings, target state/alignment combinations and four clock epochs.
- Seven caller banks, all flag and interrupt-mask combinations, and low, high,
  link-register and PC sources.
- EWRAM, IWRAM and six cartridge windows as caller and target, all ROM wait
  selections, prefetch on/off and four clock epochs.
- Execution of the first target instruction for every source register.

The checks observe the actual issued caller and target opcodes, logical PC and
aligned pipeline words, CPSR/SPSR, caller banks and registers, and branch clocks.
Each candidate must match the corresponding canonical BX's branch clocks. IWRAM
branches take three clocks. All four groups retain thirty-second deadlines.
`CUPID_TEST_BIOS` adds the same groups with an external BIOS mapped; these owned
fixtures install caller state directly and do not execute firmware startup.

`rom.thumb-exchange` runs a checksum-pinned, unchanged cartridge. It requires
the issued `4780h` branch, entry into its ARM target, the original R1=4 CMP,
the original display checkpoint and the issued idle terminal with R12=0.
The 360-frame, 150M-step and 90-second guards remain fixed. An optional external
BIOS repeats the same program and result protocol. The fetch command retains
the test input's permission notice beside the private cartridge.

That cartridge establishes the tested branch behavior. It does not measure LR,
every reserved operand combination, or complete physical bus/DMA/IRQ timing.
The owned register and timing matrices qualify the selected decoder behavior;
they do not prove hardware behavior for every reserved encoding.
