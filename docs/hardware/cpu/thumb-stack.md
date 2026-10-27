# Thumb stack transfers

An empty Thumb POP register list transfers one word from the aligned stack
address into PC and advances SP by 64 bytes. The loaded address remains in
Thumb state; its low bit is discarded. CupidGBA refills both target pipeline
slots before the next instruction issues. The transfer preserves the caller's
flags, privilege bank, interrupt masks, saved status and LR.

The empty-list path uses the existing multiple-load bus sequence and internal
load cycle. POP with an explicit PC advances SP by the actual register count.
The shared memory transfer and normal POP paths retain their timing.

## Regression scope

The `arm.thumb-stack-*` groups check:

- 7,168 empty/explicit-PC pairs across seven banks, all flag and interrupt-mask
  combinations, and every low-bit combination of stack and target addresses.
- 8,192 pairs across EWRAM, IWRAM and six cartridge caller/target windows,
  both RAM stack regions, all ROM wait selections, prefetch on/off and four
  clock epochs.
- 256 pairs that issue the first target instruction.
- 2,044 normal POP scenes covering every nonempty low-register list and all
  lists with an explicit PC, with all four stack alignments.

Each pair checks the issued opcode, target PC and pipeline words, SP
writeback, loaded and preserved registers, banked state, untouched stack
memory and matching bus clocks. IWRAM caller/stack/target transfers with only
PC take five clocks in the current bus model. The owned fixtures install CPU
state directly; mapping an optional external BIOS repeats the matrices without
executing firmware startup. Each group has a thirty-second deadline.

`rom.thumb-stack` runs a checksum-pinned, unchanged cartridge. It observes the
original `BC00h` instruction, timer comparison against `16h`, SP comparison
against `3Ch`, display checkpoint and issued idle terminal with R12=0. Its
360-frame, 150M-step and 90-second guards are fixed. An optional external BIOS
repeats the same program. The input fetch retains its permission notice beside
the private cartridge.

That cartridge checks one original timer/stack scenario. The wider owned
matrices check deterministic emulator behavior; they do not establish every
physical bus, DMA or IRQ interaction for empty register lists.
