# CPU read feedback at DMA handover

A completed halfword load can leave data on the CPU input bus before a waiting
DMA takes ownership. Reconstructing that input from instruction prefetch loses
the data read. The memory subsystem now retains the physical input word before
alignment rotation or sign extension and publishes it before releasing the bus.

The halfword path distinguishes these ports:

- OAM supplies its full aligned word, including the half not returned to the CPU.
- IWRAM changes the selected halfword bank and retains the other bank's data.
  Timed CPU and DMA word transfers seed both bank values.
- The external halfword ports repeat the sampled halfword across the input word.
- BIOS reads preserve the existing read-protection policy.
- Unmapped reads do not drive a new value.

A later DMA read supersedes the retained CPU input. The next completed opcode
fetch returns to the existing instruction-fetch feedback path. Untimed observer
reads do not change either the CPU input or the IWRAM bank history. Reset clears
both, and separate emulator instances own separate state.

The implementation lives in `src/gba/memory/feedback.c`. Bus timing, transfer
activation, interrupt deadlines and instruction expectations are unchanged.

## Verification

The native handover matrix covers 512 scenes across eight mapped windows, four
halfword address offsets, all four DMA channels and four clock epochs. Each
scene primes IWRAM through a real word DMA, activates another DMA during the
CPU read, then checks the complete captured word, destination memory and bus
release. Ownership checks cover untimed observers, unmapped reads, the next
fetch, reset and separate instances. Another 32 scenes retain BIOS protection
through both execution regions, all address lanes and four clock epochs.

Six pinned original cartridges check CPU/DMA interaction, IWRAM, OAM, protected
sources, odd signed-halfword loads and unmapped reads. The runner requires each
original comparison instruction, its operand and the completed terminal. One
cartridge does not set its failure result on every path, so a zero terminal
alone is not sufficient. Inputs and their comparisons remain unchanged.

Fetch and configure these optional inputs with:

```sh
python3 tests/roms/check_bus_feedback.py --fetch --directory .local-research/test-roms
cmake -S . -B build -DCUPID_BUS_FEEDBACK_PROBE_DIR="$PWD/.local-research/test-roms"
ctest --test-dir build -R '^(gba.memory-feedback-|rom.bus-feedback-)' --output-on-failure
```

`CUPID_TEST_BIOS` adds the same six checks through an authorized external BIOS.
The cartridges retain a 360-frame, 150-million-step and 90-second process limit.

## Remaining limits

This work covers halfword data-read feedback and the word transfers that seed
IWRAM history. It does not establish a complete physical bus model. Byte and
word CPU input feedback, register-specific I/O feedback, narrow IWRAM writes,
instruction-fetch bank history and other partially driven buses need separate
qualification. Broader CPU, DMA and display timing requirements remain open.
