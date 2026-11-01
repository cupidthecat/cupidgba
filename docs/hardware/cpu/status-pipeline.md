# ARM-state status writes and prefetched instructions

An `MSR CPSR` instruction that stays in ARM state preserves the following
instructions already in the pipeline. Updating flags or changing a permitted
control field does not reread those instruction words from memory. This also
applies when User mode ignores a control-field write.

A store can change the backing memory for an instruction that was fetched
earlier. Sequential execution still uses the fetched opcode. A branch refills
the pipeline and observes the replacement. Rereading the two queued words
after MSR incorrectly makes a status write act as a code synchronization step.
The reload also invokes the opcode-read callback without another bus cycle.

Register and immediate CPSR transfers retain their existing field masks,
register-bank changes and IRQ sampling. Their ordinary instruction fetch
continues through the bus scheduler. These regressions keep the processor in
ARM state; state-bit writes and exception returns have separate behavior.

## Regression coverage

`arm.msr-pipeline-*` runs guest instruction sequences from both EWRAM and
IWRAM. A guest store replaces a prefetched `MOV R2,#1` with `MOV R2,#2`.
The checks require the originally fetched instruction after register and
immediate MSR forms, permitted control writes, and ignored User-mode writes.
An ordinary instruction provides a retention control. A branch provides a
refill control and must execute the replacement.

Each scene observes all three issued instruction addresses and opcodes. It
checks the stored replacement, result register, fetch-read count, flags,
privilege mode and interrupt mask. The instruction read callback must see only
the initial refill and the three scheduled fetches, with two additional reads
for the branch control. No original cartridge bytes or expectations are changed.

The [ARM7TDMI Technical Reference Manual, section 6.5](https://documentation-service.arm.com/static/5e8e1323fd977155116a3129)
describes PSR transfers as data operations whose PC is never a source or
destination. The sequential prefetch proceeds during the operation; a PC write
invalidates and refills the pipeline.

This coverage establishes the specified instruction sequences. It does not
establish complete status-transfer, timing, self-modifying-code or game
compatibility.
