# Interrupts

IE, IF and IME updates become visible one cycle after a bus write or peripheral
request. IE retains its low 14 bits, IME retains bit 0, and writing one to an
IF bit acknowledges that request. Byte writes merge with the pending IE value
or acknowledge only the selected IF byte. The reserved IME byte has no effect.

After register acceptance, the enabled-request signal reaches HALT one cycle
later. The CPU IRQ line follows two CPU clocks after acceptance. With no held
bus cycle this keeps the existing system-clock deadlines. Both signals retain
values already sampled into their pipelines, including short assertions
followed by deassertions. A register update cannot replace an older
enabled-request publication due on that same clock. This matters when a DMA
acknowledgement coincides with a separately phased peripheral request: HALT
must still observe the one-clock enabled-request pulse. The register, HALT
and CPU-line stage counts remain unchanged.

CPU bus waits hold the IRQ synchronizer. A stretched memory transfer completes
one CPU clock at its ready edge; its intermediate wait clocks advance the
peripherals. Waiting for DMA to release the bus completes no CPU clock by
itself. CPU internal cycles and HALT continue to clock the synchronizer during
DMA. IF register acceptance and the HALT enabled-request signal keep their
system-clock deadlines throughout these waits.

An input pulse that begins and ends while the CPU clock is held is not captured
by its first stage. A value captured before the hold still reaches the final
stage on the next CPU-ready edge. A scheduled ready-edge event preserves this
ownership without splitting peripheral dispatch batches. Timer, DMA, audio and
existing bus-progress observations retain their previous dispatch partitions.

The CPU-line slots track scheduled and held ownership. A general bus hold sets
the held-clock state, including when no line publication needs moving. Empty
domains avoid the queue walk; a new input accepted during the hold still keeps
its remaining CPU stages. Resume releases the clock and queues each retained
publication at its original remaining deadline. Register acceptance and HALT
availability keep their independent system-clock events.

One CPU request owns the ready event. Dispatch removes it before invoking its
callback, so its next insertion can use `cupid_timing_queue_absolute` without a
cancellation search. This API requires the event to be absent from both pending
lists. General scheduling still removes a possibly queued event before moving
it; absolute scheduling uses the supplied modular timestamp directly. The
priority order, late-callback clock and deferred queue rules apply to both paths.

IME and the CPSR IRQ mask control exception entry; HALT can wake
from an enabled request while both masks prevent entry.

The CPU samples the IRQ line and its latched CPSR mask at instruction
boundaries. An instruction that clears the CPSR mask is followed by one
ordinary instruction before an already asserted line can enter the handler.
A pipeline refill refreshes that mask latch. IRQ entry consumes a discarded
instruction fetch, saves CPSR and the return address, enters ARM IRQ mode,
and refills the exception vector. `subs pc, lr, #4` restores the saved state
and resumes the interrupted ARM or Thumb instruction.

HALT accepts a timed control write one internal clock after its data strobe.
An immediate wake retains the IRQ line sampled before that acceptance
clock; a later wake samples the line before the wake clock. A line that becomes
asserted during these clocks is accepted after the resumed instruction. An
already asserted line can enter immediately, subject to the sampled CPSR mask.
These clocks advance peripheral events. Untimed observer accesses keep their
existing clock position, and console reset clears the retained HALT sample.

Timed MMIO reads and writes dispatch due peripheral events after the data-bus
cycle. Loads retain their final internal cycle. Untimed observer accesses do
not advance the CPU. Timer interrupts keep their original overflow timestamps
during late event dispatch and across the 32-bit scheduler clock wrap.

Native regressions cover these transitions and CPU access widths. The local
external-BIOS timer suite checks 90 IRQ observations and 936 count-up results;
see [testing](../development/testing.md). The CPU's pipeline and exception
registers are described in the
[ARM7TDMI technical reference manual](https://documentation-service.arm.com/static/5e8e1323fd977155116a3129).

`gba.irq-pulses` checks every publication in a sequence that previously lost
the enabled-request pulse. It covers repeated queue reuse, single-clock and
bulk dispatch, 32-bit clock wrap, both CPU instruction modes, IME/CPSR masks,
actual DMA acknowledgement strobes, STOP clock freezing and console reset.
The old core fails the pulse, HALT-resume and DMA controls. These regressions
preserve the documented pipeline; they do not settle physical IRQ sampling at
every instruction/refill boundary. Remaining original CPU/bus cartridge
failures stay tracked in #10.

Four `gba.irq-clock-*` timing groups add 2,056 observed waveform clocks, complete
CPU load/store data, repeated unclocked input pulses, already captured stages,
real DMA transfers, multiply internal-cycle overlap and reset. Memory regions,
both fetch widths, wait controls and four clock anchors retain their existing
transfer durations. The preceding core fails all four groups. Four original
external-firmware load, branch and store cartridges now reach their encoded
IRQ return addresses. The selected [fast entry](bios/reset-entry.md) now
aligns the initial display phase with full firmware boot, correcting the
formerly built-in-only interaction failures. The HALT/DMA timer comparison
remains open; the OAM variant depends on the selected display-memory arbiter.
See [CPU/DMA/IRQ interactions](cpu/dma-irq-interactions.md) and
[display bus waits](cpu/display-irq.md).

The additional observer group rejects skipped comparisons, wrong opcodes or
modes and missing visits in owned ARM/Thumb streams that still report zero.
The optional external-firmware IRQ profile requires all 46 original comparison
outcomes across twelve cartridges and an actually issued terminal. The former
slow return timeout now reaches its three comparisons and passing terminal.
Eleven cartridges pass. One retained failure reads `0xF4` instead of `0xF5`;
its original wide guard encoding does not record that failure on ARM7TDMI,
so its zero guest result alone is insufficient. The strengthened observer
rejects it. No input bytes or decoder behavior were changed for that encoding.

Queued IF changes retain their acceptance deadlines and apply assertions and
acknowledgements in request order. Timed MMIO dispatches a due timer overflow
before its data write. An IF acknowledgement on that clock therefore clears
the pending assertion at their shared acceptance deadline. An acknowledgement
on the preceding clock cannot publish the later assertion early, and a later
acknowledgement preserves any line transition already in flight. Reassertions
after a queued acknowledgement remain pending. This is the selected logical
ordering; the relative electrical phases still have no new hardware capture. See
[DMA and bus ownership](dma.md) for transfer arbitration and its tested limits.
Game Pak prefetch interactions and precise STOP wake-up latency need separate
hardware coverage. STOP now freezes the system clock and accepts enabled
external wake sources; see [serial and GPIO](serial/normal-and-gpio.md).
Both BIOS paths pass the listed count-up and IRQ suite on the pinned local input.

## Event dispatch and host clock observers

The ordinary timing loop reads the next event deadline before entering its
dispatcher. Due callbacks retain the existing priority order, lateness and
unlink-before-callback ownership. A deferred future queue is restored before
the tick returns or calls a host clock observer. An observer can cancel, add
or defer an event, so the loop computes the returned CPU deadline again after
that observer. An event newly made due by the observer waits until the next
tick, including a tick that advances zero clocks. Linked clock coordinators
retain their own synchronization path.

The nine scheduler checks include observer queue changes, deferred callbacks,
owned rescheduling and signed, 32-bit and 64-bit clock-wrap boundaries. The
same complete check output is retained against a matching preceding core.

## Atomic waits with an empty CPU-line pipeline

An occupied request consumed in one call can retain its final ready state
directly when no CPU-line stage is pending and no peripheral event is due
before or at that ready edge. The CPU handler, release callback, context and
release priority must have their owned identities. Deferred queues, active
callbacks, linked clock coordinators, foreign observers, intermediate data
strobes and dynamic readiness predicates retain the ordinary event path.

The final board tick consumes the full original wait and advances the owned
renderer at its ordinary completed clock. The release event retains its
dequeued timestamp and empty link. A custom CPU handler observes the held
clock before release; it also receives ordinary bus-entry, completion and
partial-advance calls even when no board clock is due. These calls publish
ARM or Thumb open bus and clear the early-exit flag.

The `gba.irq-clock-quiet-atomic` group compares 2,880 scenes across six wait
lengths, three relative CPU positions, five clock epochs, eight event/IRQ
conditions, two clock-observer configurations and two reference handlers.
It checks complete renderer buffers, latches and memory, IRQ state and both
event queues at the ready edge and after following clocks. Forwarding clock
observers and CPU handlers provide the ordinary-path controls. These are
host-dispatch equivalence checks; the original reboot deadline, cartridge
comparisons and full platform profiles remain separate requirements.
