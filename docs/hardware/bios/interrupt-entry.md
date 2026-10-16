# Firmware IRQ entry

The built-in firmware's IRQ vector at `0x18` branches to `0x128`. Its ARM
callback returns through `0x138`. The [hardware reference](https://problemkaputt.de/gbatek.htm#biosinterrupts)
describes these addresses and the six-register frame.

The wrapper saves R0-R3, R12 and the exception LR on the IRQ stack, sets R0 to
`0x04000000`, and loads the callback pointer through the IWRAM mirror at
`0x03FFFFFC`. The callback enters ARM state with LR=`0x138`. It may return
through that link or the fixed slot. The wrapper restores its six registers
and returns with `SUBS PC,LR,#4`, restoring the interrupted CPSR and instruction
state. The callback must preserve any other registers it uses. Nested handlers
remain responsible for saving their own interrupted exception state.

The IRQ body uses its normal instruction and bus costs. Fixed service-layout
padding preserves the other firmware entry points and does not execute during
IRQ dispatch. After the exception return, protected BIOS reads retain the
last fetched `0xE55EC002` word from the adjacent SWI dispatcher. The complete
16 KiB image and its additive identity are reproduced from owned source.

Three native groups run 5,712 complete IRQ transactions per firmware path:
2,688 ARM callers, 2,688 Thumb callers and 336 timer/DMA/HALT interactions.
They cover all seven privilege modes, all NZCV combinations, both FIQ-mask
states, static/timed clients, ordinary/literal callback returns and clock wrap.
They require the actual entry, callback and restore instructions, the complete
six-word frame, caller registers/status, normalized bank state and protected
readback. An IRQ taken in IRQ mode overwrites that bank's LR and SPSR before
the wrapper runs; the checks retain that architectural behavior.

These native fixtures explicitly prepare valid caller and IRQ stacks without
booting firmware. They do not qualify power-on state or initial display phase.
The original cartridge profile boots each firmware path and requires every
original issued comparison plus both zero-result boundaries. Its observer's
twenty owned executions reject changed input, missing comparisons, wrong
opcodes and wrong instruction modes while retaining sampled state and clocks.

The current Windows candidate passes six of eight original cartridge
runs. The built-in fast second-return case fails its first comparison, and
the external slow second-return case does not reach its comparisons within
the existing guard. These failures remain enabled and also occur on the
preceding core. Matching-header/library controls show that the address change
repairs three original built-in cartridge runs; the preceding built-in image
fails all four. The native matrix alone does not establish complete IRQ or
startup accuracy.

A private control retains the external boot and substitutes the owned image
at the first cartridge instruction. The relocated wrapper then passes all
three original fast second-return comparisons and both result boundaries;
the preceding owned image fails. Keeping the built-in initial state while
using external firmware still fails the first comparison. This distinguishes
the layout repair from the remaining initial-state/phase gap. These firmware
substitutions are diagnostic controls, not normal startup qualification.
