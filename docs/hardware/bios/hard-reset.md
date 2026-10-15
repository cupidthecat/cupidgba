# HardReset

SWI 26h begins a warm reboot and never returns to its application caller.
Its instructions mask IRQ/FIQ, clear IME through a byte bus write, establish
the Supervisor, IRQ and System stacks, and clear the 512-byte BIOS work area.
The system scheduler continues running during these operations. Timers, DMA,
video and audio retain their elapsed clocks until the firmware resets their
corresponding registers.

The built-in firmware keeps its fast-entry startup policy. It executes all
register/memory reset groups, sets POSTFLG through its byte register, runs the
sound-bias ramp, and executes the reset groups again before final bank/work
initialization and an ARM cartridge entry. EWRAM, IWRAM, palette RAM, VRAM and
OAM are cleared through ordinary timed stores. ROM, BIOS and save allocations
remain attached, and committed save bytes retain their contents and dirty
state. No host delay or synthetic animation interval is inserted. The display
clock continues through the reboot.

At the cartridge entry, R0 through R12 are zero, the System stack is
`03007F00h`, LR selects `08000000h`, and the CPSR control byte is `1Fh`.
The normal ARM pipeline contains the entry instructions. The BIOS work area,
including its temporary startup interrupt pointer, is clear. The shared bank
reset has the same saved-status readback qualification described for
[SoftReset](../bios-soft-reset.md).

The built-in startup's temporary interrupt callback records and acknowledges
enabled requests while retaining disabled requests and earlier BIOS flags.
It supports the fast startup policy. External animation and communication
interrupt dispatch remain properties of the supplied firmware.

An external BIOS executes its own complete startup program. Its animation,
header checks, communication choices and startup duration remain properties
of that input. A local authorized cartridge observation reaches the original
entry after warm startup from both ARM and Thumb callers. Further 600-frame
intro observations cover video, complete PCM and execution progress. These
bounded runs do not establish full gameplay compatibility. Multiboot,
debugger startup paths, physical reset pulses and complete startup electrical
behavior remain outside this qualification.

## POSTFLG byte ownership

A BIOS byte write to `04000300h` can set POSTFLG bit zero. Further byte writes
retain that bit, ignore unused input bits, and do not access HALTCNT at
`04000301h`. Writes originating outside BIOS are ignored. A repeated startup
flag write therefore continues executing without entering HALT or STOP.
Combined halfword and word power-register behavior retains separate
qualification limits.

## Regression scope

The sixteen `gba.bios-hard-reset-*` native groups require 1,512 observations:
336 startup-prefix cases, 112 complete built-in reboots, 16 timer/DMA/IRQ
chronology cases, 12 repeated reboots, 12 temporary-startup IRQ cases and 1,024
ARM/Thumb POSTFLG byte strobes.
Every group has a ten-second deadline. The existing MMIO group adds 1,536
byte cases across both initial flag states, every byte value and BIOS/RAM/ROM
write origins.

The prefix matrix covers all seven caller privilege modes, both instruction
states, every IRQ/FIQ mask combination, IWRAM/EWRAM/cartridge callers and clock
wrap. `CUPID_TEST_BIOS` adds 352 paired prefix/chronology observations. They
compare register and bank state, work-area clears, timer/IRQ state and elapsed
clocks up to startup entry. Internal code pointers are checked relative to
their service entries because the firmware images have different layouts.

The observed prefix clocks are:

| Caller | IWRAM | EWRAM | Cartridge with the selected wait profile |
| --- | ---: | ---: | ---: |
| ARM | 844 | 851 | 850 |
| Thumb | 844 | 848 | 848 |

These are software observations for the recorded bus configuration, not
electrically measured reset timing. The chronology cases retain original
callback deadlines through deliberate late dispatch and signed/unsigned clock
wrap, require DMA data and both completion/overflow requests, and verify that
IME suppresses exception entry. Complete reboot cases check every cleared
memory byte, retained save storage, continuous clocks, ownership, caller
replacement and the first cartridge instruction. Repeated calls use the same
machine and require deterministic durations without resetting its scheduler.

The full hardware goal remains open under
[issue 1](https://github.com/cupidthecat/cupidgba/issues/1).
