# CPU and interrupt checks

[Testing overview](../testing.md). Run commands from the repository root.

## Completion and instruction observation

The IRQ, saved-status, user-bank and generated-corpus probes share the
[completion and deadline guard](../frame-boundaries.md#cpu-probe-acceptance).
Their owned regression inputs check late DMA-held terminals without changing
any original input, expected result or execution budget.

## CP14 transfers

CP14 register-transfer timing and original cartridge comparisons are covered in
[CP14 transfers](../../hardware/cpu/cp14.md). Run `arm.cp14-*`,
`runner.cp14-probe-protocol`, `runner.status-observations` and `rom.cp14-transfer*`
for the native timing matrix and the original comparison guards.

## Instruction observers

The `runner.observations` and three `arm.observer-*` groups require actual
instruction-issue observations rather than a zero terminal alone. They use
owned ARM/Thumb streams with deliberately skipped failure writes, ordered
and repeated observations, malformed inputs, IRQ/HALT/STOP boundaries and
clock wrap. Each retains a ten-second deadline. All twenty prefetch groups
pin their 84 original timing comparison instruction addresses, bytes, modes
and complete register values on each BIOS path. Source inputs, comparisons
and run limits remain unchanged. See
[prefetch qualification](../../hardware/prefetch.md).
The bank group observes a user-bank load and both following operand phases,
checks the original bank values and results, and compares observed execution
with execution without the hook across clock wrap. Observation runs before
operand selection and must preserve the pending phase.

## Banked registers and cartridge ports

Three `arm.user-bank-gpio-*` groups require 15,360 paired RAM/cartridge-port
scenes after actual `LDM^` and `STM` instructions. They cover every adjacent
banked-register pair in the five privileged modes, all block addressing modes
and writeback choices, each cartridge window, wait controls, prefetch and
clock wrap. The first stored word retains the register read-port conflict;
the next uses the current bank after the word's two strobes complete. Every
group has a thirty-second deadline and rejects the preceding implementation.

## CPU, DMA and IRQ interactions

`CUPID_CPU_INTERACTION_PROBE_DIR` adds sixteen original CPU/DMA/IRQ cartridges
from a local input root. `CUPID_TEST_BIOS` repeats them with the external image.
Every run checks the cartridge digest, its original encoded comparison values
in issue order, the pre-display R12 result and the original terminal. The
twenty comparison instructions also retain their CPU mode and opcode checks.
The 360-frame, 150-million-step and 90-second process guards are unchanged;
CTest allows 100 seconds. All sixteen cases pass with both firmware paths in
the integrated core, including their original IRQ return-address and timer
comparisons. They remain enabled when the input root is supplied. The separate
IRQ-profile failure described below remains open. See [CPU/DMA/IRQ interactions](../../hardware/cpu/dma-irq-interactions.md).

Four `gba.irq-clock-*` timing groups require 2,056 complete waveform clocks through
stretched memory requests and real DMA. They check both fetch widths, all
cartridge wait windows, RAM wait controls, CPU load/store data, repeated input
pulses between ready edges, retained captured stages, internal multiply cycles,
reset and clock wrap. IF and HALT-availability histories keep their original
system-clock deadlines. A preserved preceding core fails all four groups.
Each group has a ten-second deadline. The existing music-progress image and
chronology expectations remain unchanged.
Three additional `gba.irq-clock-gpio-*` groups compare 11,952 complete paired
ordinary-ROM/GPIO word-request waveforms and 388,032 clock observations for
fetches, loads and stores. Both cartridge strobes retain their original
deadlines, while the CPU synchronizer advances at the complete request's final
ready edge. Each group keeps a ten-second deadline. See
[CPU/DMA/IRQ interactions](../../hardware/cpu/dma-irq-interactions.md).
The observer group independently rejects bypassed comparisons, wrong opcodes
and modes, and missing visits in ARM/Thumb streams with zero result registers.
Guarded runs match boundary-only observation runs for general and banked
registers, CPSR, the instruction pipeline, EWRAM, IWRAM, I/O and elapsed clocks.
Both paths use the issue hook; this checks those fields rather than full-state
equivalence with execution without a hook.

With `CUPID_HALT_PROBE_DIR` and `CUPID_TEST_BIOS`, twelve additional
`rom.irq-probe-*-external` cases require their 46 original comparison outcomes,
CPU modes, opcodes, pre-display results and issued, completed terminals. Original
source and cartridge bytes are pinned; a byte-identical local rebuild locates
the observations without changing the code. One input has a wide conditional
guard encoding that is not a conditional branch on ARM7TDMI. Its observer
checks the preceding comparison's Z flag at the actual issued halfword, so the
guest cannot silently skip failure recording. The slow-return case passes;
the `0xF4` versus `0xF5` comparison remains a failure. The original execution
guards and 90-second process deadline remain, with 100 seconds in CTest.

Fetch the checksum-pinned IRQ cartridges and their license with:

```sh
python3 tests/roms/check_irq_probe.py --fetch --directory .local-research/irq-inputs
cmake -S . -B build-irq -DCUPID_BUILD_FRONTEND=OFF \
  -DCUPID_HALT_PROBE_DIR="$PWD/.local-research/irq-inputs" \
  -DCUPID_TEST_BIOS=/path/to/gba-bios.bin
cmake --build build-irq --target test_irq_clock
ctest --test-dir build-irq -R '^rom.irq-probe-' --output-on-failure
```

This fetch supplies the twelve IRQ inputs; the four separate HALT inputs
still need their own local files when running the complete HALT profile.
Existing files with different bytes are rejected and preserved. Repeated
fetches retain verified cached inputs. The IRQ selection currently reports
eleven passes and the original branch/IRQ failure; the command does not mark
that failure as expected or change its comparison.

## Generated instruction corpus

The five optional `rom.cpu-corpus-*` groups check 50,000 generated ARM/Thumb
records, retaining every original comparison and failure branch. The native
`runner.cpu-corpus-observer` group checks the read-only observer protocol and
rejects incomplete or corrupted transcripts. See
[generated CPU corpus](../cpu-corpus.md) for immutable inputs, complete counts,
firmware paths and execution bounds. These checks qualify generated instruction
results rather than CPU timing.

## Thumb branches and stack transfers

The four `arm.thumb-exchange-*` groups compare 28,160 canonical/candidate BX
pairs, including reserved H1 encodings, caller banks, memory windows and target
execution. `rom.thumb-exchange` retains the original cartridge's branch,
comparison, display checkpoint and terminal. Its input is fetched by
`python tests/roms/check_thumb_exchange.py --fetch --directory .local-research/test-roms`.
See [Thumb branch exchange](../../hardware/cpu/thumb-branch-exchange.md) for
the selected decoder policy, reserved-encoding limits and fixed run guards.

`arm.thumb-stack-*` checks empty-list POP pipeline refill, SP writeback,
register banks, alignment, bus clocks and every normal POP register list.
`rom.thumb-stack` retains the original cartridge's timer/SP comparisons,
display checkpoint and terminal. Fetch its input with
`python tests/roms/check_thumb_stack.py --fetch --directory .local-research/test-roms`.
See [Thumb stack transfers](../../hardware/cpu/thumb-stack.md) for coverage and
the fixed run guards.

## Display bus and CPU clocks

The four `gba.display-irq-*` groups retain 4,608 complete display transactions
and per-clock IRQ waveforms across both bus widths, three display regions,
sixteen port phases and four clock epochs. Short pulses, persistent levels,
captured stages and assertions accepted on the ready edge use separate
ten-second checks. They retain full data payloads,
physical clocks and internal-cycle counts. See
[display waits and CPU IRQ sampling](../../hardware/cpu/display-irq.md).
The ten `gba.uart-*` groups check 12,288 complete format-matrix frames, 124,928
bit-center samples, FIFO capacity, errors, IRQs, connected peers, DMA/timer
chronology and lifecycle. Owned ARM/Thumb guests each require 1,024 transferred
bytes, full RAM output and guards, BIOS HALT wake and their passing terminal.
Two corrupt-input controls must complete and reject the changed byte.
`CUPID_TEST_BIOS` adds three external-image groups. Each retains a thirty-second
deadline and the guest retains its million-step and three-million-clock guards.
Hosted Clang reconstructs the owned driver. See [UART](../../hardware/serial/uart.md)
for the nominal baud model and remaining physical qualification.

## User-bank transfers

The six `arm.user-bank-*` groups check 840 complete user-bank transfers, operand
phases, bank ownership, ALU flags, memory and branch behavior, predicates,
reset/IRQ entry and real DMA/timer progress across clock wrap. All retain
ten-second deadlines. `CUPID_USER_BANK_PROBE_DIR` names the local input root
containing the `LDM` directory and adds eight original local
cartridges, and `CUPID_TEST_BIOS` repeats them with the external image. The
observer requires all 35 original failure guards, the FIQ pre-display result
and the issued R0 terminal. Owned negative controls reject bypassed failure
recording while retaining guest state and clocks. See
[user-bank LDM](../../hardware/cpu/user-bank-ldm.md) for the scope and input guards.

## Exception and reset entry

The six general `arm.undefined-*` groups cover exception-entry clocks, caller banks
and ARM/Thumb return, predicates, clock wrap and the internal-cycle timer/DMA
interval. Five additional `arm.undefined-bkpt-*` groups require 391,436
observations of unsupported breakpoint encodings, exception return, complete
immediates, predicates, explicit host hooks and peripheral progress. Both
timed and static paths retain their opcode fetch costs. All eleven groups
retain ten-second deadlines; see [undefined entry](../../hardware/cpu/undefined-entry.md).

The six `arm.cp14-*` groups check decoded register transfers, complete selector
coverage, r15 flags, predicates, retained undefined exceptions and timer/DMA
progress with owned instruction streams. Each has a ten-second deadline;
see [CP14 register transfers](../../hardware/cpu/cp14.md).

The four `arm.reset-entry-*` groups check guest-visible reset control, prior
ARM/Thumb and privilege state, pending IRQs, repeated reset and the built-in
fast entry with owned instructions. Each retains a ten-second deadline. Native
BIOS service fixtures prepare their application caller explicitly without
booting firmware or changing peripheral clocks; see [reset entry](../../hardware/bios/reset-entry.md).

## IRQ pulse ownership

`gba.irq-pulses` requires queued enabled-request values to survive a later
register update on the same publication clock. It checks complete signal
histories through queue reuse and scheduler wrap, bulk versus single-clock
dispatch, HALT resume in both CPU modes with either mask state, DMA
acknowledgements, STOP and console reset. It keeps the existing IRQ stage
delays and does not replace the original CPU/bus cartridge checks. See
[interrupt ownership and remaining sampling limits](../../hardware/interrupts.md).

## Saved status

Set `CUPID_SAVED_STATUS_PROBE_DIR` to the private source directory containing
`psr/psr.gba` to register the saved-status cartridge. `CUPID_TEST_BIOS` adds its
external BIOS path. The checker verifies the pinned 2052-byte input, all five
original ARM comparisons, their operands and privilege modes, the original
display checkpoint and terminal instruction. Both result boundaries must issue
with R12=0, and the terminal must finish before the frame and clock limits.
The dedicated probe retains the 360-frame, 150-million-step,
660-frame clock ceiling and 90-second process limits. It does not change the
generic headless runner's observation protocol or distribute the cartridge.

The ten `arm.saved-status-*` groups require 1,139,864 observations across
reads, register/immediate writes, masks, predicates, banks, waits, clock wrap,
static/timed clients and timer/DMA/IRQ interactions. Five
`arm.instruction-observer-*` groups cover initialization/reset ownership,
ARM/Thumb issue, all ARM predicates, HALT/STOP and real timer IRQ preemption.
Their owned programs reach zero-result boundaries even when changed input or
an IRQ return bypasses a required comparison. The observer rejects those runs,
wrong opcodes/modes/privileges/operands, missing or reordered comparisons and
duplicates. Sampled register/bank/status, pipeline, RAM, I/O and clock equality
checks verify passive observation; these samples do not describe every piece
of emulator state. See [saved-status transfers](../../hardware/cpu/saved-status-transfers.md).

## Native IRQ coverage

Interrupt regressions cover register visibility, byte masks and acknowledgements,
successive IRQ line changes, HALT wake-up with IME clear, the CPU's sampled
CPSR mask, Thumb exception return, ARM/Thumb MMIO access widths, DMA completion
bits for all four channels, and late dispatch across clock wrap.
## Readable I/O input handover

The six `gba.io-feedback-*` groups exercise 3,296 native scenes through the
memory callbacks and real DMA transfers. The byte, halfword and word groups each
cover 960 scenes: fifteen register pairs, four address lanes, all four DMA
channels and four clock epochs spanning counter wrap. They check CPU rotation,
the aligned input sample, the waiting DMA's destination and latch, and retained
local IWRAM history.

The floating-port group preserves the existing read-phase policy in 108 scenes.
The 180 observer scenes require untimed reads to leave CPU input and local RAM
history intact. Another 128 scenes seed an owned UART receive queue, read its
data and acknowledged status at every access width, and require exactly one
consumed byte before DMA handover. The next queued byte remains available.

These checks use ten-second CTest deadlines. They qualify the selected register
retention model, not physical pin behavior or complete instruction timing. See
[I/O input retention](../../hardware/io-registers.md#cpu-input-retention).
