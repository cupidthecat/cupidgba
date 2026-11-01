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

The selected fast display phase satisfies all eight original cartridge runs
on Windows. The external slow second-return case retains all three original
comparisons and both zero-result boundaries with the CPU timing model. The
previous built-in fast second-return mismatch (`0xDC` versus `0xDE`) came from
an early initial display phase: it resumed one extra Thumb instruction after
the handler. Aligning that phase repairs the original comparison without
changing the firmware wrapper or its instruction clocks. The delivered base
firmware-layout fix remains separately qualified for its recorded scope.
Matching-header/library controls show that moving the wrapper repairs three
original built-in cartridge runs; the preceding built-in image fails all four.
The native matrix alone does not establish complete IRQ or startup accuracy.

A private control retains the external boot and substitutes the owned image
at the first cartridge instruction. The relocated wrapper then passes all
three original fast second-return comparisons and both result boundaries;
the preceding owned image fails. With the earlier built-in initial display phase,
using external firmware still failed the first comparison. This distinguished
the layout repair from the former display-phase gap. These firmware
substitutions are diagnostic controls, not normal startup qualification.
