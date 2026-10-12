# Interrupts

IE, IF and IME updates become visible one cycle after a bus write or peripheral
request. IE retains its low 14 bits, IME retains bit 0, and writing one to an
IF bit acknowledges that request. Byte writes merge with the pending IE value
or acknowledge only the selected IF byte. The reserved IME byte has no effect.

After register acceptance, the enabled-request signal reaches HALT one cycle
later. The CPU IRQ line follows two cycles after acceptance. Successive line
changes keep their own deadlines, including short assertions followed by
deassertions. IME and the CPSR IRQ mask control exception entry; HALT can wake
from an enabled request while both masks prevent entry.

The CPU samples the IRQ line and its latched CPSR mask at instruction
boundaries. An instruction that clears the CPSR mask is followed by one
ordinary instruction before an already asserted line can enter the handler.
A pipeline refill refreshes that mask latch. IRQ entry consumes a discarded
instruction fetch, saves CPSR and the return address, enters ARM IRQ mode,
and refills the exception vector. `subs pc, lr, #4` restores the saved state
and resumes the interrupted ARM or Thumb instruction.

Timed MMIO reads and writes dispatch due peripheral events after the data-bus
cycle. Loads retain their final internal cycle. Untimed observer accesses do
not advance the CPU. Timer interrupts keep their original overflow timestamps
during late event dispatch and across the 32-bit scheduler clock wrap.

Native regressions cover these transitions and CPU access widths. The local
external-BIOS timer suite checks 90 IRQ observations and 936 count-up results;
see [testing](../development/testing.md). The CPU's pipeline and exception
registers are described in the
[ARM7TDMI technical reference manual](https://support.arm.com/documentation/ddi0210/c/).

The relative priority of an IRQ assertion and an IF acknowledge on exactly
the same hardware cycle remains unverified. The current pending-register
model preserves requests reasserted after an acknowledge is queued. DMA
arbitration, Game Pak prefetch interactions and STOP wake-up need separate
hardware coverage. The built-in BIOS interrupt-wait path also has remaining
count-up failures documented in the testing guide.
