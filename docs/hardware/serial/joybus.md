# JOY commands and peer synchronization

CupidGBA handles all four JOY commands through
`cupid_serial_joybus_command`. A connected peer supplies a complete request
at the current emulated timestamp after dispatching preceding events. The
peer owns frame timing and synchronization with the other machine. This API
does not advance the CPU, generate electrical pin edges, or open a network
connection. JOY mode requires both high RCNT mode bits; another mode or STOP
rejects the request without changing state.

| Command | Request bytes | Reply bytes | Register effect |
| --- | --- | --- | --- |
| `00` status | Command | `00 04 status` | Retains data and flags |
| `FF` reset | Command | `00 04 status` | Sets JOYCNT reset flag |
| `15` write to GBA | Command and four data bytes | Status | Publishes JOY_RECV and receive flags |
| `14` read from GBA | Command | Four data bytes and status | Sets send complete and clears send ready |

Data words travel least significant byte first. The interface receives
complete bytes; it does not model the most significant bit first order within
each electrical byte. Wrong request lengths, unknown commands, null buffers
and insufficient response capacity return zero without changing registers,
interrupts or output bytes. Valid requests return the reply length. Request
and response buffers may overlap.

JOY_RECV retains the last complete incoming word, including when another
word arrives before software consumes it. A guest read acknowledges the
receive-ready flag in JOYSTAT bit 1. Writes to JOY_TRANS stage outgoing data
and set send-ready bit 3. Guest reads of the send registers retain zero,
as required by the unchanged source register suite. The peer's read command
replies with the retained word even when ready is clear.
Its response captures status before acknowledging the send buffer. The
general-purpose status bits 4 and 5 survive commands and acknowledgements.
Reset commands set the reset event flag without erasing data or status;
console reset clears the JOY registers through the ordinary serial reset path.

JOYCNT bits 0, 1 and 2 record reset, receive and send events independently.
Guest writes acknowledge selected event bits with ones. Bit 6 enables the
serial interrupt for reset, receive and send completion; status requests do
not request interrupts. SIOCNT's interrupt bit does not control JOY commands.
Requests use the existing interrupt publication, availability, CPU-line and
HALT paths at the supplied command timestamp. No extra transfer delay is
invented by the command interface.

The register and command tables are described in the programming manual,
version 1.35, sections
[13.5.2 through 13.5.4](https://www.manualslib.com/manual/3342081/Nintendo-Game-Boy-Advance.html?page=149)
and
[13.5.5 through 13.5.9](https://www.manualslib.com/manual/3342081/Nintendo-Game-Boy-Advance.html?page=150).
The command interface and existing guest access-width model are software
behavior. Subword electrical acknowledgement, status sampling within a read
response, partial wire packets, clock recovery, cable waveforms and physical
latency remain unmeasured. No host-console transport or JOY boot loader is
provided by this interface. UART and full link compatibility remain open.

## Verification

Eight native groups cover command and acknowledgement sequences, every value
in every payload byte lane, overlapping buffers, all 256 command bytes and
malformed lengths/capacities, decoded ARM/Thumb register accesses, IRQ masks,
HALT, STOP, console reset and mode isolation. Timed incoming frames share the
real scheduler with DMA receive reads and timer interrupts; single-clock and
late dispatch must produce the same complete data and interrupt results
through both clock-counter wraps.

The owned ARM and Thumb drivers each complete 32 receive/send rounds at four
clock origins. Each checks incoming data, records all words and round counts,
sends the complement, acknowledges receive and interrupt flags, executes the
firmware HALT service and resumes from the peer's send-complete interrupt.
Both passing terminals and all RAM records are required. Changing one input
bit must reach the failing terminal. An optional external BIOS runs the same
unchanged fixture. These fixtures enter application code directly and do not
qualify BIOS boot or physical JOY timing.

Rebuild and verify the owned driver with Clang and LLD:

```sh
clang --target=arm-none-eabi -march=armv4t -fuse-ld=lld -nostdlib \
  '-Wl,-Ttext=0x03000000,--image-base=0x03000000,--oformat=binary,-e,0' \
  tests/gba/serial/joybus-driver.s -o joybus-driver.bin
python3 tests/gba/serial/generate_joybus_driver.py joybus-driver.bin
```

The checked-in byte image is owned test code. Hosted Clang checks reconstruct
it from assembly. See [testing](../../development/testing.md) for source-input
and platform qualification.
