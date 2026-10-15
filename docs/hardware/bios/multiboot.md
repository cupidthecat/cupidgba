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
not add inferred host-side validation. Tests use owned EWRAM, IWRAM and ROM
bodies, including a ROM page crossing and the complete maximum body. They
validate retained parameter bytes, unchanged sources and surrounding guards.

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

Twenty-seven native BIOS groups cover normal downloads, one-, two- and
three-client multiplayer downloads, sparse client selection, incorrect offsets
and CRC, readiness waits, rejected client markers and disconnection. A local
`CUPID_TEST_BIOS` repeats every group with an authorized external image.
Fixtures check ARM/Thumb callers, System/IRQ banks, EWRAM/IWRAM parameters,
clock wrap, complete retained parameter images, guarded receive buffers,
decrypted body bytes and independent timestamped wire-transcript signatures.

The guard matrix records 960 bounded observations per firmware image across
six mode values, RAM parameter regions and their mirrors, both caller ISAs,
source addresses and masked length boundaries. Its deliberately unready peer
rejects before malformed payloads are read; these cases do not qualify complete
transfers at malformed lengths. Eight read-only parameter cases verify that
ROM remains unchanged. Twenty-four cases reject enabled DMA across all four
channels, three transfer modes and both caller ISAs.

Two active-download cases inject a real DMA source mutation after entry. The
receiver validates the changed payload and CRC. Timer and DMA interrupt flags
become pending while the sender masks CPU requests; the timer handler runs
after return and preserves the caller. One-cycle observers capture the
separate work-area and scratch stores before return. Both firmware images
match the asserted bus chronology and timestamped wire signatures. Selected
multiplayer clients with incorrect offset or CRC cause failure; an incorrect
offset from an unselected client is ignored.

The separate multiplayer group covers all four baud selectors, ordered
receive slots, participant counts, slave IDs, latched local words, IRQ
publication, cancellation/reset, late dispatch, STOP, unassigned endpoints and
two synchronized boards in either advancement order. The unchanged original
serial suite retains all 90 register observations, four counted normal timing
cases and its four original multiplayer skips. Those skips do not qualify
connected multiplayer timing.

Small BIOS fixtures retain their two-million-instruction guard because the
sender's initial delay alone exceeds 524,000 instructions. The new full-size
`maximum` profile sends all `3FF40h` bytes and checks every plaintext word,
16-bit offset wrap, final CRC, parameter bytes and buffer guards. It has a
separate 150-million-instruction guard and 180-second deadline; ordinary
fixtures retain their ten-second deadlines. Existing source programs, expected
values, instruction guards and deadlines remain unchanged.

External firmware, cartridge inputs, captures and private research are not
distributed. This corpus qualifies the stated software exchanges and bus
observations. It does not establish calibrated cable phases, general link,
UART or JOY support, or complete BIOS accuracy. Platform/runtime and hosted
delivery results are tracked in #71.
