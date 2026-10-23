# Normal serial transfers and expansion-port pins

CupidGBA supports normal 8-bit and 32-bit transfers through its core pin API.
Internal clock periods are 64 or 8 CPU clocks per bit. Output shifts most
significant bit first on falling SC edges; input is sampled on rising edges.
The selected width and clock are latched when a transfer starts. Data registers
retain their previous value until completion, which publishes received data,
clears the start bit and requests an enabled serial interrupt. The 8-bit result
preserves the upper byte of the data register.

The completion path includes six clocks beyond the nominal bit periods.
The unchanged local timing suite observes 633, 185, 2169 and 377 timer clocks
for the four normal width/rate combinations, including its CPU, IRQ and HALT
overhead. Both BIOS paths match these observations. The implementation places
the six-clock interval after the last sample; these aggregate measurements do
not establish its electrical placement or the first edge's absolute phase.

External clock mode waits for incoming SC edges. It does not complete from
elapsed time alone. Devices provide driven pin masks and levels through
`cupid_serial_set_input`; `cupid_serial_set_output_callback` reports output
levels and directions at emulated timestamps. Callers must dispatch preceding
events before applying an input change. Native tests connect two core instances
and exchange distinct 8-bit and 32-bit values. The SDL2 frontend connects a pair
through [local sessions](local-session.md). Network transport and physical
multiplayer-cable wiring remain separate from that logical pin connection.

In general-purpose mode, RCNT selects each pin's direction and output level.
Input pins expose driven levels or their pull-up level. A falling SI input
requests the serial interrupt when RCNT bit 8 is set. SI configured as an output
does not generate that input interrupt. Normal mode drives SD low for an
external-clock receiver; an internal-clock sender leaves SD as an input.

STOP freezes the system clock and leaves pending peripheral deadlines in place.
CPU stepping and frame execution return while stopped, allowing host input
polling to continue. An enabled keypad or cartridge request can resume the
clock; serial wake is available in general-purpose mode. Normal serial clock
edges do not advance a stopped transfer. IME and the CPSR mask continue to
control exception entry after wake. Oscillator settling and electrical wake
latency remain unqualified. HALT keeps peripheral clocks running and can wake
from serial completion with IME clear.

Mode-specific register masks and access are checked by all 90 unchanged local
source observations. Multiplayer receive registers are read-only; normal
32-bit data registers accept writes in their mode. Byte writes preserve
untouched fields and do not restart a busy transfer. JOY control status bits
use write-one acknowledgement. Connected multiplayer word transfers are
described in [multiboot](../bios/multiboot.md); complete JOY command peers are
described in [JOY commands](joybus.md). UART framing and flow control are
described in [UART](uart.md), including its remaining physical timing limits.

The CPU samples the IRQ line before HALT's wake clock. If the line becomes
asserted during that clock, the resumed instruction completes before IRQ entry.
An already asserted line can enter immediately. A timed HALTCNT write also
accepts one internal clock after its data strobe and retains the earlier IRQ
sample for an immediate wake. Native regressions cover both caller modes,
return addresses, reset and clock wrap.

Nine unchanged standalone cartridges pass their IRQ timer, received-data and
one-clock start-bit comparisons with both BIOS paths. Four further HALT probes
pass their timer and saved return-address comparisons with an external BIOS.
Those four observe firmware-internal addresses and require that BIOS input;
the replacement firmware places some internal routines at different addresses.

The [programming manual](https://archive.org/download/NintendoGbaManualV1.1/Nintendo%20Gba%20Manual%20V1.1.pdf)
describes mode selection, pin polarity and power states in chapters 13 and 16.
It requires selecting the shift clock before setting start and warns that
simultaneous selection can cause extra shifts. Further
[serial timing measurements](https://tasvideos.org/Forum/Topics/24013?CurrentPage=6)
describe clock-relative timing and port activation differences on test hardware.
Physical cable behavior, unmeasured startup transitions and complete protocol
compatibility remain open. See [testing](../../development/testing.md).
