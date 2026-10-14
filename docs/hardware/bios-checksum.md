# Firmware checksum

GetBiosChecksum (SWI 0D) reads all 4,096 words of CupidGBA's embedded 16 KiB
firmware through the normal CPU and memory bus. It adds each word modulo
32 bits, masks IRQ/FIQ during the read loop and returns through the ordinary
SWI stack. A timer or DMA can progress during the calculation. Pending
interrupts become eligible again when the caller's status is restored.

R0 receives the sum, R1 receives one and R3 receives `0x4000`. The shared
dispatcher restores the saved registers and caller status. A call made in
SVC mode overwrites that same bank's LR and saved status through normal SWI
entry; the regressions check those results separately from other caller banks.
The isolated IWRAM call requires 41,011 clocks in both ARM and Thumb modes.
Caller memory and wait controls change the entry and return bus costs.

The embedded image has an additive identity of `0xBAAE187F`. A generated
word in the reserved final slot supplies that identity from the owned
assembly and data. The checksum routine calculates it from the image;
it does not return a host constant. The generator verifies all 4,096 words,
and the assembler output must match every embedded byte. This identity is
compatibility metadata. It does not establish identical firmware bytes,
boot behavior or complete BIOS equivalence.

## Regressions

The corpus checks 180 observations per BIOS path: 84 caller-bank/mask/wrap
cases, 60 caller-region/wait cases, 24 changed-image/read-only cases and
12 timer/DMA/IRQ cases. All seven caller banks run ARM and Thumb calls with
both initial IRQ/FIQ mask settings. SVC's exception state, the other saved
status banks, returned registers, stack, protected reads, timer values and
clock wrap have explicit checks.

Changed-image cases initialize diagnostic words near the start and end of
the image and require the corresponding wrapped checksum change. Guest
stores to BIOS must leave those words intact. These cases reject a fixed
checksum result while retaining the full read-loop timing.

Interaction cases run a timer through repeated overflows, transfer data
with DMA before or during the calculation, and check the mask's effect on
IRQ delivery. An overdue callback retains its deadline and measured lateness.
The tests sample the read cursor, require deferred requests after return and
compare complete return/IRQ state, timers and callback observations with a
local external BIOS when `CUPID_TEST_BIOS` is configured.

Eleven groups have separate ten-second deadlines and require their complete
case counts. External BIOS files are local inputs and are not distributed.
Other BIOS services, raw firmware identity and physical system timing remain
separate qualification work.
