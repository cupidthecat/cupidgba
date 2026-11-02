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

A later DMA read supersedes the retained CPU input. A Thumb opcode fetch from
IWRAM replaces only its addressed halfword bank. Data in the other bank survives
until a transfer drives that bank. ARM fetches replace the complete word. BIOS
and OAM supply their full aligned word for either instruction width. Other
fetch regions retain the existing pipeline reconstruction. Untimed observer
reads do not change either the CPU input or the IWRAM bank history. Reset clears
both, and separate emulator instances own separate state.

CPU word reads from BIOS, IWRAM and OAM publish their complete input before
releasing the bus. An unaligned load returns the rotated register value while
the retained input stays aligned. IWRAM also updates both local banks. BIOS
protection is applied before publication, and addresses outside the BIOS
storage do not drive a new input. These rules use the decoded device after
internal-memory remapping. Observer reads with no cycle counter retain input
and bank history.

Byte reads from BIOS and OAM likewise retain the complete aligned input word.
Byte selection and signed-load extension affect only the CPU register result.
BIOS reads use the selected protected word when execution is outside the BIOS.
This extends the same port-retention policy used for halfword reads; it does
not infer byte behavior for IWRAM or the external ports.

Opcode publication follows the actual sample, including untimed pipeline
refills before timed execution. It does not guess an opcode before the memory
read. Timed IRQ entry also samples its discarded fetch without executing the
opcode or adding a cycle. This keeps explicit pipeline setup and ordinary
execution consistent.

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

Opcode checks add 640 scenes across five execution regions, ARM/Thumb mode,
alignment, clock epochs and timed/untimed initial refills. They include RAM/OAM
mirrors and wrap boundaries, all three cartridge windows and the BIOS end. Another 64 complete
instruction pairs check data retained after halfword and word loads, including
unaligned word rotation. Thirty-two DMA cases retain the other IWRAM bank after
a transfer, then check the next fetched opcode, loaded value, destination word,
address progress and bus release. Removing opcode publication makes the BIOS,
OAM, retained-load and DMA groups fail without changing their expectations.
Eighty IRQ-entry scenes require the discarded sample followed by both vector
samples, while preserving entry clocks, return address, status and pipeline
contents. Omitting the discarded read fails this group.

The word handover group adds 768 scenes across BIOS, IWRAM and OAM, four
mapping settings, four byte offsets, all DMA channels and four clock epochs.
It checks the CPU return value separately from the complete DMA input and
destination word. BIOS protection and RAM/OAM aliases remain enabled. The
word ownership group covers timed and legacy cycle-counted reads, observer
peeks, unmapped reads and reset. The handover group fails without word
publication. This extends the existing input-retention model; the reference
port widths and software checks do not establish new physical handover
measurements.

Byte checks add 512 DMA handovers using the same mapping, address, channel and
clock matrix on BIOS/OAM. Another 256 scenes execute ARM and Thumb unsigned
and signed byte loads, checking the register value, unchanged flags, full
input word and BIOS protection. Ownership checks also include timed and
legacy byte reads, observer isolation and undriven addresses. The byte
handover check fails before byte publication is enabled. These are software
regressions for the selected port model, not physical captures of byte reads.

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

This work covers halfword data-read feedback, byte data reads from BIOS/OAM,
word data reads from the three 32-bit internal ports and the word transfers
that seed IWRAM history. It does not establish a complete physical bus model.
Byte feedback on IWRAM and external ports, word
feedback from external ports, register-specific I/O feedback, narrow IWRAM writes,
cross-region input history and other partially driven buses need separate
qualification. The fast-RAM and BIOS/OAM fetch rules follow the documented
[GBA unused-memory behavior](https://problemkaputt.de/gbatek.htm#gbaunpredictablethings);
the checks here are software regressions, not new physical hardware captures. Broader CPU, DMA and display timing requirements remain open.
