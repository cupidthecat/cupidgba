# Cartridge X/Y tilt sensor

The `KHPJ`, `KYGJ`, `KYGE` and `KYGP` board headers select a twelve-bit X/Y
sensor alongside EEPROM. The sensor uses the save-bus byte interface, separate
from cartridge GPIO. Known board wiring takes precedence over an unused RTC
library marker. Explicit device configuration remains available.

## Registers and snapshots

| Byte address | Operation |
| --- | --- |
| `0x0e008000` | Write `0x55` to arm a conversion |
| `0x0e008100` | Write `0xaa` while armed to capture both inputs |
| `0x0e008200` | Read X bits 0–7 |
| `0x0e008300` | Read X bits 8–11 and ready in bit 7 |
| `0x0e008400` | Read Y bits 0–7 |
| `0x0e008500` | Read Y bits 8–11 |

The `0x0f` save window exposes the same offsets. Halfword and word reads
replicate the selected byte; writes use the ordinary save-bus selected-byte
rules. Each access retains its configured SRAM wait. The sensor ports neither
autodetect SRAM nor modify saved bytes. EEPROM continues through its separate
serial address range.

CupidGBA currently uses a functional snapshot model: the command pair captures
both inputs at the second write's bus strobe and ready is always asserted.
It does not emulate a measured conversion interval. Input changes leave that
snapshot intact until another complete command pair. An unarmed `0xaa` does
nothing. Other command values are ignored without clearing an existing arm.
Unimplemented addresses continue through the ordinary save path.

Reset, device configuration and cartridge replacement clear the arm and set
both retained readings to `0xfff`. External input values survive these actions.
Changing tilt presence does not reset GPIO, RTC or motor output.

## Input

Both runners accept `--tilt auto|on|off`, defaulting to `auto`, and
`--tilt-x 0..4095` / `--tilt-y 0..4095`. Each input defaults to 928 (`0x3a0`).
These are raw converter levels, not a calibrated angle or acceleration.
Decimal integers are required. Invalid input stops startup.

In the SDL2 frontend F1 lowers X, F2 raises X, F3 lowers Y and F4 raises Y.
Each step is 32 and clamps at the endpoints. F10 restores both nominal values.
These keys change external input; they do not replace an existing snapshot.
Host accelerometers are not connected by this manual input path.

Core callers use `cupid_cartridge_set_tilt_sample()` to update the pair
atomically. If either axis exceeds twelve bits, neither input changes.

## Validation and limits

Native checks cover all 4,096 values, both complete axis bytes, command state,
all four board headers, both save windows and ARM/Thumb byte, halfword and word
instructions. They check EEPROM transfers and the complete saved image,
unchanged SRAM bytes, independent GPIO/motor state, reset, replacement and
unused RTC markers. Scheduled input changes on either side of a CPU command
strobe check snapshot ownership during late dispatch and clock wrap. Platform
checks cover strict parsing, live keys and preservation of the other axis.

Eight generated ARM/Thumb drivers each require 32 complete paired observations,
including ready and reserved bits, at VBlank cadence. They configure slow SRAM
waits and PHI at 4 MHz, poll readiness with a bound, and require the complete RAM
record and passing terminal. A disabled-device control must fail every pair.
The cartridges initialize their CPU modes and stacks because explicit guest
entry skips BIOS boot. Neither that path nor the generated header qualifies
the BIOS boot sequence.

The [hardware register description](https://www.akkit.org/info/gbatek.htm)
describes a busy flag and clock setup, but does not establish a conversion
duration for this model. Physical busy duration, PHI dependence, analog
calibration, power-on values, undocumented aliases and malformed command
electrical behavior remain unqualified. Immediate readiness is a software
model choice, not a hardware measurement. No speculative conversion delay is
added. Delivery is tracked in #75; full cartridge acceptance remains open in #1.
