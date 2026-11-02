# Original register and timing probes

`tests/roms/register-probes.json` pins ten unchanged cartridges at revision
`66f5f1d60cd7030b600606886c76f5c821ecaf9d`. The fetch command verifies each
SHA-256 and retains the pinned permission notice beside the inputs. It preserves
existing inputs and rejects conflicting cached data.

```sh
python3 tests/roms/check_register_probe.py --fetch --directory /path/to/probes
cmake -S . -B build -DCUPID_REGISTER_PROBE_DIR=/path/to/probes
cmake --build build
ctest --test-dir build -R 'register-' --output-on-failure
```

Set `CUPID_TEST_BIOS` to an authorized external BIOS to add the external-boot
checks. BIOS images are local inputs and are not fetched or redistributed.

| Cartridge scope | Original comparisons | Registered boot paths |
| --- | ---: | --- |
| Banked STM followed by ALU operations | 5 | Replacement and external BIOS |
| Palette byte-write replication | 4 | Replacement and external BIOS |
| VRAM byte-write replication | 4 | Replacement and external BIOS |
| Timer disable/re-enable overflow edge | 2 | Replacement and external BIOS |
| BIOS copy timing | 1 | Replacement and external BIOS |
| DMA started from BIOS | 4 | Replacement and external BIOS |
| Long DMA timing | 1 | Replacement and external BIOS |
| Empty-list Thumb PUSH | 2 | Replacement and external BIOS |
| Timer value retained from BIOS boot | 3 | External BIOS |
| Power-on divider phase | 10 | Manual diagnostic; phase remains unresolved |

Each run must execute every original comparison in order, reach the original
pre-display checkpoint with a zero result, and complete the original terminal
branch with that result preserved. The banked-register probe reports through
`r0`; the others use `r12`. A later zero does not excuse a failed or skipped
comparison. Register comparisons observe both live operands. Literal operands
also require their original address construction, load instructions and ROM
value. Original image hashes, comparison opcodes, result routines and execution
budgets remain enforced.

Owned negative controls execute wrong measurements, corrupted expected operands,
equal but incorrect operands and skipped comparisons. Each can produce a zero
terminal result without observations; the complete checker rejects it. The
controls cover both supported result registers and do not modify corpus inputs.

## Boot-dependent limits

The power-on probe explicitly tests the divider phase after power-on, using the
timer's divide-by-64 clock. Its source notes that loader timing can change the
result. Current execution fails its unchanged comparisons with both BIOS paths:
the replacement path first differs at the third comparison (`0` versus `1`),
and external boot first differs at the first (`1` versus `0`). The failure logs
and original expectations remain required evidence. No timer offset or boot
padding has been introduced to match this one program.

Run that diagnostic explicitly; a failure remains a failure:

```sh
python3 tests/roms/check_register_probe.py --directory /path/to/probes \
  --runner build/cupidgba-headless --test power-on-divider
```

The boot-retention probe reads the stopped timer before enabling it. External
boot retains the expected `0x8B` and passes all three comparisons. Replacement
boot starts with zero and fails that precondition; the checker therefore
requires an external BIOS for this specific cartridge. This does not qualify
the replacement boot's power-on timer residue or divider phase.

The registered selection passes 17 runs when an external BIOS is configured.
The broader exploratory selection recorded 17/20 when it also executed the two
power-on phase cases and the unsupported replacement-boot retention case.
These results do not replace the separate 71/76 CPU/bus/FIFO/IRQ selection or
resolve its five existing failures. Full timer, firmware and whole-system
accuracy remain open.
