# Runner result boundaries

The headless runner observes comparisons and result checkpoints through the
CPU's instruction-issue hook. IRQ arbitration, pipeline refill and HALT/STOP
polling cannot satisfy an observation merely by exposing a prefetched address.
An ARM checkpoint records the selected result register before that instruction
executes, so later guest code cannot conceal its value.

An expected terminal is the original ARM idle branch `EAFFFFFEh`. It must issue
at the requested address and complete its return to the same logical PC and
idle pipeline. A retained ARM A1 bit is preserved. The runner reports the
actual issue and completion separately:

```text
terminal-issued=yes completed=yes pc=08000318
terminal=yes r12=0 result=PASS
```

An instruction can issue before its fetch suspends in HALT. The runner retains
that issue while `cpuInstructionPending` is true and accepts completion only
after the pending instruction returns. Resuming that fetch must not fabricate
a second instruction issue. Frame, step, clock and process guards remain those
selected by each test; reaching a prefetched terminal does not bypass them.

## Regression scope

Three `runner.terminal-*` groups check 7,168 completed-branch scenes across
seven banks, all flags and masks, ARM A1, four clock epochs and zero/nonzero
results. Negative controls reject a corrupted completed pipeline and a
terminal discarded by IRQ entry, or left unissued by HALT/STOP. Eight real
HALT suspension/resume cases require one issue and a completed return under
the same budgets. These owned fixtures install CPU state directly and do not
qualify firmware startup or physical timing. Each group has thirty seconds.

`runner.observations` separately requires an observed terminal after all owned
comparisons. Its seven-step negative program reaches that terminal in the
prefetch pipeline without issuing it and must fail. Existing missing, reordered,
repeated, wrong-value and exact-capacity controls remain active.

The pinned Thumb exchange and empty-POP cartridges include their unchanged
terminal opcodes as final register observations. Their existing caller,
comparison, checkpoint, input-digest and execution guards remain checked.
The extra completed idle branch consumes normal emulated bus clocks; it does
not change any original timer expectation or emulator timing constant.

## Masked status observations

`--expect-status-at ADDRESS MASK VALUE` checks CPSR bits at actual instruction
issue, in the same ordered stream as register observations. The mask must be
nonzero and the expected value must contain no bits outside it. Output reports
`masked-cpsr`, the mask and the expected value, together with the issued address,
CPU mode and opcode. Missing, extra or reordered visits fail the run even when
the guest leaves zero at its result boundaries. Reading status has no emulated
side effects.

Owned controls cover every NZCV combination, selected flag and control masks,
wrong values with zero guest results, mixed register/status ordering, absent
visits and invalid argument bounds. The CP14 cartridge uses this observation
to check its original negative-flag guard after a register transfer to r15.

## Repeated observation sequences

`--repeat-observations MIN` repeats the complete registered sequence and requires
at least `MIN` complete cycles. The runner continues checking every watched
instruction through the selected execution boundary, including visits after the
minimum. Wrong values or order fail the run permanently. A correct partial
prefix at the frame boundary is reported separately and does not count as a
complete cycle. This mode requires observations and a positive unsigned 64-bit
minimum; repeated minimum options are rejected before loading the cartridge.
Ordinary observations retain their exact, nonrepeating sequence contract.

`runner.observation-cycles` covers complete, missing, reordered and late-failing
sequences, one-site sequences and invalid arguments. A zero guest result cannot
conceal an observation failure. See [cyclic display controls](../hardware/render/cyclic-controls.md)
for the original display loop and the limits of this evidence.
