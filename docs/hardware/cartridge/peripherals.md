# Cartridge solar, gyroscope and rumble devices

CupidGBA connects these devices to the cartridge GPIO data, direction and
read-control registers. GPIO remains visible in the three lower ROM windows;
the underlying cartridge bytes remain unchanged. Disabling register reads
restores ROM reads while device writes continue.

## Solar sensor

Pin 0 clocks the counter on a falling edge. Pin 1 clears it while high,
including when a falling clock edge occurs at the same time. Repeated levels
do not count. The counter has twelve bits; its low eight bits drive the
converter, so the flag can fall again after 256 pulses. Pin 3 reports whether
the converter output exceeds the current sensor threshold. Pin 2 does not
select this circuit. RTC communication shares pins 0 and 1; GPIO directions
and the RTC output remain part of the physical pin values.

The [cartridge circuit description](https://www.akkit.org/info/gbatek.htm)
and [counter component specification](https://www.ti.com/lit/ds/symlink/sn74lv4040a.pdf)
describe this edge, clear and converter arrangement. CupidGBA implements its
logical transitions. Propagation delays, comparator hysteresis, optical
response and a calibrated lux scale remain unmeasured.

`cupid_cartridge_set_solar_level()` accepts a deterministic relative input
from 0 through 255. It maps to threshold `255 - level`, and changes the
comparator immediately without restarting the counter. More light therefore
requires fewer pulses. A zero input leaves the flag low; 255 raises it after
one pulse. Console reset clears the counter and port but preserves this input
and the cartridge device configuration.

The frontends use `--solar auto|on|off` and `--sunlight 0..255`, defaulting to
automatic detection and input 128. In the SDL2 frontend F5 lowers the input
by 16 and F6 raises it by 16, clamped to the available range. The core starts
with input 0 until its caller supplies a level.

Automatic detection recognizes header codes `U3IJ`, `U3IE`, `U3IP`, `U32J`,
`U32E`, `U32P` and `U33J` as solar/RTC boards. This selects physical devices;
it does not alter guest execution.

## Rumble

Pin 3 controls the motor's logical state. The core exposes that state and
reports changes through `cupid_cartridge_set_rumble_output()`, with the
emulated clock of the GPIO strobe. CPU and DMA word accesses retain separate
halfword strobes. Repeated writes of the same level produce no new edge.
The reported clocks describe bus transitions; motor response and force have
not been calibrated against a physical cartridge.

Automatic detection recognizes `V49J`, `V49E`, `V49P`, `2GBP`, `RZWJ`, `RZWE`
and `RZWP`. The last three boards also provide the
[serial gyroscope](gyroscope.md). `--rumble auto|on|off` controls motor presence
independently of that converter.

Console reset, device disable, cartridge replacement and core destruction
stop the motor. Replacing or removing an output subscriber stops its previous
host output; the logical cartridge state is retained for a new subscriber.
Cartridge replacement preserves the subscriber and external sensor inputs,
then detects the new board with a cleared port.

Loading a replacement ROM reads and validates its candidate image before
releasing the current ROM. A failed open, seek, allocation or read preserves
the attached image, port, motor, battery/save state and CPU mapping. A
successful replacement stops the previous board and rebinds a cached CPU ROM
mapping to the new allocation. The caller still resets the console before
executing a newly loaded cartridge.

The SDL2 frontend uses the connected controller's rumble output when supported.
It renews a 250 ms effect every 100 ms while the emulated motor is enabled.
The effect expires if the frontend stalls. Controller removal and shutdown
stop the host output; reconnecting a controller resumes the current logical
state. Controllers without rumble support leave core execution unchanged.
These host intensity and duration choices are frontend policy, not cartridge
motor measurements.

## Regression coverage

The memory-mapped [X/Y tilt sensor](tilt.md) uses the save bus instead of GPIO.
It has independent device selection, raw input, snapshots and validation.

Native component checks cover all 256 levels and two complete counter wraps,
including 2,097,152 pulse observations. Driver checks execute ARM/Thumb byte,
halfword and word instructions, all three GPIO ROM windows, prefetch on/off,
five input levels, reset, repeated levels, live input and shared RTC use.
Interaction checks cover DMA sample strobes and completion IRQ, prefetch
history, bulk dispatch and clock wrap. Motor checks cover port directions,
read control, repeated writes, timed CPU/DMA edges and output ownership.
An invalid replacement image must leave the previous cartridge intact; a
successful replacement must bind the CPU cache to the new image.

Generated cartridges run both ARM and Thumb loops, require all 256 comparisons
per device and verify the complete timestamped motor trace. These probes use
an explicit guest entry with the selected BIOS mapped. Their generated images
do not contain a boot logo, and these checks do not qualify the BIOS boot
sequence. The ordinary boot and gameplay suites remain separate.

The SDL2 regression uses a virtual controller to observe host motor output,
finite-effect expiry, sunlight keys, removal by instance ID, reconnection,
reset and subscriber shutdown. This verifies the frontend calls; it does not
measure a physical controller or cartridge motor.
