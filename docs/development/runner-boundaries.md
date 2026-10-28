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
