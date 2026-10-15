# Interrupts

IE, IF and IME updates become visible one cycle after a bus write or peripheral
request. IE retains its low 14 bits, IME retains bit 0, and writing one to an
IF bit acknowledges that request. Byte writes merge with the pending IE value
or acknowledge only the selected IF byte. The reserved IME byte has no effect.

After register acceptance, the enabled-request signal reaches HALT one cycle
later. The CPU IRQ line follows two cycles after acceptance. Both signals
retain each accepted value and its publication deadline, including short
assertions followed by deassertions. A register update cannot replace an older
enabled-request publication due on that same clock. This matters when a DMA
acknowledgement coincides with a separately phased peripheral request: HALT
must still observe the one-clock enabled-request pulse. The register, HALT
and CPU-line delays remain unchanged.

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
[ARM7TDMI technical reference manual](https://support.arm.com/documentation/ddi0210/c/).

`gba.irq-pulses` checks every publication in a sequence that previously lost
the enabled-request pulse. It covers repeated queue reuse, single-clock and
bulk dispatch, 32-bit clock wrap, both CPU instruction modes, IME/CPSR masks,
actual DMA acknowledgement strobes, STOP clock freezing and console reset.
The old core fails the pulse, HALT-resume and DMA controls. These regressions
preserve the documented pipeline; they do not settle physical IRQ sampling at
every instruction/refill boundary. Remaining original CPU/bus cartridge
failures stay tracked in #10.

The relative priority of an IRQ assertion and an IF acknowledge on exactly
the same hardware cycle remains unverified. Queued IF changes retain their
acceptance deadlines and apply assertions and acknowledgements in request
order. Reassertions after a queued acknowledgement remain pending. See
[DMA and bus ownership](dma.md) for transfer arbitration and its tested limits.
Game Pak prefetch interactions and precise STOP wake-up latency need separate
hardware coverage. STOP now freezes the system clock and accepts enabled
external wake sources; see [serial and GPIO](serial/normal-and-gpio.md).
Both BIOS paths pass the listed count-up and IRQ suite on the pinned local input.
