# Soft reset

SoftReset (SWI 00) executes replacement firmware through the CPU and memory
bus. It reads the restart byte at `03007FFA` before clearing the upper
`200h` bytes of IWRAM. A zero byte selects cartridge entry `08000000`;
every nonzero byte selects EWRAM entry `02000000`.

The routine sets the supervisor stack to `03007FE0`, the IRQ stack to
`03007FA0` and the system stack to `03007F00`. It clears the supervisor and
IRQ links and writes zero to their saved status registers. The clear runs
as 128 ascending Thumb word stores from `03007E00` through `03007FFC`.
Timers and DMA continue during this work. The latched destination survives
later DMA writes to the restart byte.

On entry to the selected address, visible R0 through R12 are zero, SP is
the system stack base and LR is the selected address. CPSR is `1Fh`: ARM
system mode with cleared condition flags and IRQ/FIQ masks. BX refills the
ARM pipeline from the selected memory region. The routine does not return
to the calling instruction stream.

SoftReset preserves EWRAM, IWRAM below the cleared area, palette RAM, VRAM,
OAM and device configuration, apart from ordinary caller/exception stack
writes and running devices. Applications must disable IME before the call:
the clear erases the IRQ handler pointer while its Thumb loop enables the
CPU's IRQ input. Pending timer and DMA requests remain pending when IME is
clear. SoftReset does not replace RegisterRamReset's memory and device resets.

## Qualification

The native corpus checks 768 observations:

- All 256 restart bytes from ARM and Thumb cartridge callers: 512 cases.
- Three restart bytes across seven caller banks, ROM/EWRAM/IWRAM callers,
  ARM/Thumb state and two cartridge wait settings: 252 cases.
- ARM/Thumb calls with DMA before or after the restart-byte load: four cases.

Each case checks complete visible registers, architectural banked registers,
saved status, the two fetched entry instructions, retained memory and I/O,
the protected BIOS read word, elapsed clocks and the timer counter. It executes the first entry instruction
and its following branch, requiring the destination marker and rejecting
the stale caller marker. A default ARM cartridge call takes 878 clocks to
the ROM pipeline or 876 clocks to the EWRAM pipeline, before executing the
first entry instruction.

DMA cases sample all 512 work-area bytes at one-clock intervals and require
at least 128 observed changes. A DMA word changes the restart byte before
or after it is latched. Both timer and DMA interrupt requests must be pending,
the transfer must finish and IME must remain clear. With `CUPID_TEST_BIOS`,
the same cases compare complete results and sampled write times against a
local external BIOS. No BIOS dump or cartridge image is distributed.

These are comparisons within CupidGBA's CPU and bus model. In particular,
its current MSR implementation forces saved-status bit 4 on, so an MRS after
the firmware's zero write reads `10h` in the supervisor and IRQ banks on both
BIOS paths. The corpus requires that current result; it does not establish
the physical CPU's readback for this write. Hardware qualification of that
status-register edge remains part of the ARM audit. FIQ, abort and undefined
bank state is compared, rather than assuming those banks are reset. IRQ
execution during the clear, arbitrary caller stack locations and untested
bus contention still need separate qualification.
