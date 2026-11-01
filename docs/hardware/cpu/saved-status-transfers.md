# Saved-status transfers

User and System modes have no SPSR bank. In these modes, `MRS Rd,SPSR`
reads the live CPSR. Register and immediate forms of `MSR SPSR` ignore the
write. The read alias does not make CPSR a write destination, and a bankless
write does not alter an exception mode's saved status.

FIQ, IRQ, Supervisor, Abort and Undefined modes retain their separate saved
status. Their transfers keep the existing control/flag field masks and
mode-bit handling. `MRS Rd,CPSR` and ordinary CPSR writes use their existing
paths.

Every transfer retains its normal instruction fetch cost. An ignored write
still consumes that fetch, including static execution without timed memory
callbacks. It does not change the IRQ mask or add an internal cycle. Timers,
DMA and pending interrupts continue through the normal scheduler.
IRQ entry selects the existing static exception path when no timed fetch
callback is installed.

The [ARM7TDMI Technical Reference Manual](https://documentation-service.arm.com/static/5e8e1323fd977155116a3129)
describes the register banks and saved status in the programmer's model.
The available original status cartridge additionally checks the System-mode
alias, a separate Abort-mode saved value, return to System mode, mode bit 4
and the final interrupt-timing comparison. Qualification must require all
five issued comparisons and both original result boundaries, with either
BIOS path. A zero result after bypassing a comparison is insufficient.

The optional `instructionObserver` callback runs at ARM/Thumb issue after
the system has handled IRQ/HALT arbitration. It reads the current opcode,
registers and status without changing guest state or emulated time. Its
host context survives reset. `cupid_arm_instruction_address` supplies the
physical opcode address at this boundary; raw R15 and instruction operand
addresses retain their separate meanings.
ARM predicates that fail still produce one observation. A system step in
HALT or STOP produces none until an instruction issues; an accepted IRQ
reports the vector instruction, rather than the discarded pending opcode.
The API observes ordinary public step calls. Synthetic instruction helpers
and direct exception-entry calls do not themselves issue a guest instruction.
Clients must rebuild against the matching CPU/GBA structure layout.

Ordinary transfers use R0 through R14 as the data register. Invalid modes,
PC destinations, power-up reserved bits and other unpredictable status or
exception-return combinations require separate evidence.

See [ARM-state status writes](status-pipeline.md) for retention of prefetched
instructions across ordinary CPSR updates.
