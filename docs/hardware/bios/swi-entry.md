# Software interrupt entry

An executed ARM or Thumb SWI saves the caller's CPSR in SPSR_svc, selects the
Supervisor register bank, sets the IRQ mask and enters ARM state at vector
08h. LR_svc points to the instruction after the SWI. Flags and the FIQ mask
retain their caller values. The vector and following word refill the pipeline
through the normal memory bus.

The CPU enters this exception for every service byte. Firmware then reads the
caller instruction to select a service. CupidGBA uses this path with both its
built-in image and an external BIOS. ARM condition checking happens before
exception entry; an instruction whose condition fails only advances the
caller pipeline. ARM's remaining immediate bits do not change exception entry.
These rules follow sections 2.8.7, 2.8.9 and 6.12 of the
[ARM7TDMI technical reference manual](https://documentation-service.arm.com/static/5e8e1323fd977155116a3129).

## Regression scope

`gba.bios-swi-entry-*` executes owned caller instructions and stops after one
decoded instruction. The five groups contain 18,176 scenes per BIOS path:

- `arm` and `thumb`: all 256 service bytes, four interrupt-mask combinations,
  varied flags and four clock epochs, including 32-bit clock wrap boundaries.
- `banks`: all seven caller modes, all flag and mask combinations, in ARM and
  Thumb state, with defined and out-of-table service numbers.
- `predicates`: all 16 ARM conditions, all flag combinations, three service
  numbers and four values for the remaining immediate bits.
- `sources`: EWRAM, IWRAM and both halves of all three cartridge wait regions,
  all wait selections for the caller region, prefetch enabled and disabled,
  ARM and Thumb callers, and four clock epochs. Out-of-table entry clocks must
  equal the defined service's entry clocks for the same caller configuration.

Checks include the issued opcode, saved status, banked registers, return
address, IRQ mask, vector pipeline words and total emulated clocks. IWRAM
entry takes three clocks; a failed ARM predicate takes one. An optional
`CUPID_TEST_BIOS` adds the same five groups for a local external BIOS.

These fixtures install caller state directly. They do not qualify firmware
boot, interrupt arrival during entry, DMA arbitration, or completion of an
out-of-table service. Such a service can dispatch beyond the defined table;
the resulting execution depends on the selected firmware image. The checks
make no claim that this later execution matches another BIOS.
