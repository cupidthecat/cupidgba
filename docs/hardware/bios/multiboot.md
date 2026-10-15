# BIOS multiboot sender

SWI `25h` executes the download sender through the replacement firmware's CPU
and bus. R0 points to a parameter block. R1 selects normal 32-bit mode at
256 kHz (`0`), multiplayer 16-bit mode (`1`), or normal 32-bit mode at 2 MHz
(`2`). R0 returns zero for the observed successful exchange and one for a
rejected exchange. The service preserves the tested caller registers, bank,
stack and saved status through the normal SWI wrapper.

The calling program performs client discovery, header transfer, palette and
initial handshake before this service. The service sends the body length,
encrypted body, readiness requests and final CRC. An initial ready marker in
the receive registers is part of that prepared calling state; a successful
return from an empty parameter block is not a download.

## Parameters and transfers

The caller-provided fields used by the fixtures are:

| Offset | Field |
| --- | --- |
| `14h` | Handshake byte for normal mode |
| `19h`–`1Bh` | Client seed bytes |
| `1Ch` | Palette byte |
| `1Eh` | Selected clients in bits 1–3 |
| `20h`, `24h` | Body source and end pointers |

The ordinary protocol uses body lengths from `100h` through `3FF40h`, in
16-byte units, excluding the previously transferred header. The sender's
observed size masking and guarded source reads are retained; the routine does
not add inferred host-side validation. Tests use owned bodies in the prepared
RAM regions and validate retained parameter bytes and surrounding guards.

The sender masks CPU interrupt requests while it executes. Peripheral clocks
continue, including serial completion, timers and DMA. It rejects the observed
enabled-DMA starting state. The five initial scratch words and work words at
`38h`, `3Ch` and `40h` receive the final result; other parameter payload remains
subject to the observed individual byte/halfword stores.

Each word advances a rolling integer key and a bitwise CRC. The normal and
multiplayer paths use their distinct initial values, polynomial and XOR key.
Encryption includes the destination address beginning at `020000C0h`.
Multiplayer sends the low and high halfwords separately. The sender checks
returned destination offsets, handles `65h` requests and `74h` readiness waits,
then exchanges `66h` and the final CRC. Register writes and busy polling execute
as ordinary instructions; no host function copies the body to a receiver.

## Connected serial model

Normal modes use the existing SC/SO/SI pin interface. The owned receiver feeds
each SI bit on a falling clock edge and observes every transmitted word. It
decrypts the body independently and checks all received bytes and the final
checksum.

Multiplayer uses the serial device API in `include/cupid/serial.h`. A peer
configuration supplies a player ID, contiguous connected-player mask and an
exchange callback. The callback receives a latched outgoing word and provides
the other players' words. The serial engine schedules receive-slot publication
in order, retains busy state through completion and raises the enabled IRQ at
the modeled timestamp. Incoming slave frames use the same scheduling and latch
the slave's current send register. Callers synchronize the participating cores
before submitting a frame; the API does not advance another core implicitly.

An unassigned endpoint retains a written Start request but has no cable clock
owner. It generates no receive words or completion interrupt. An assigned child
cannot initiate its own frame. Mode cancellation, device reconfiguration and
reset cancel pending events. Reset retains caller-owned device connection
context. STOP freezes the board clock, and stopped children reject new incoming
frames through this API.

Completion intervals depend on baud and the final connected player slot.
They are modeled software intervals, not new cable measurements. Receive-slot
phases use nominal eighteen-bit periods and omit unmeasured inter-player gaps.
The electrical pin waveform, transient connection behavior, precise gaps and
timeout calibration remain unqualified. Successful word-level downloads do not
establish those physical properties or complete link emulation.

## Qualification fixtures

Twelve native BIOS groups cover four valid normal downloads, one- and
three-client multiplayer downloads, source guards, incorrect offsets and CRC,
readiness waits, rejected client markers and disconnection. A local
`CUPID_TEST_BIOS` repeats every group with an authorized external image.
Fixtures check ARM/Thumb callers, System/IRQ banks, EWRAM/IWRAM parameters,
clock wrap, complete retained parameter images, guarded receive buffers,
decrypted body bytes and independent timestamped wire-transcript signatures.

The separate multiplayer group covers all four baud selectors, ordered
receive slots, participant counts, slave IDs, latched local words, IRQ
publication, cancellation/reset, late dispatch, STOP, unassigned endpoints and
two synchronized boards in either advancement order. The unchanged original
serial suite retains all 90 register observations, four counted normal timing
cases and its four original multiplayer skips. Those skips do not qualify
connected multiplayer timing.

The BIOS fixtures have a separate two-million-instruction guard because the
sender's initial delay alone exceeds 524,000 instructions. Existing guards,
source programs, expected values and test deadlines remain unchanged.
External firmware, cartridge inputs, captures and private research are not
distributed. Broader source/length and bus-interaction qualification, final
platform/runtime checks and hosted delivery gates remain tracked in #71.
