# DMA and bus ownership

Ordinary DMA requests become ready two clocks after the trigger; Direct Sound
refill requests use three clocks. Once the CPU releases the bus, the engine
spends one handover clock, then schedules each read and
write separately. It samples the source when the read completes and publishes
the destination when the write completes. One release clock follows the last
transfer. Timers, video events, audio requests and interrupt signals continue
to run throughout the transfer.

CPU fetches and data accesses wait for DMA ownership to end. Internal CPU
cycles can overlap the DMA phases. Swap instructions lock both data accesses,
so a request cannot split the read from its corresponding write. A pending
load internal cycle completes before a branch refills its pipeline.

Channel 0 has the highest priority. Arbitration occurs after each complete
read/write pair. A higher-priority request can interrupt a lower-priority
burst at that boundary; the suspended channel retains its addresses, remaining
count and data latch. Chained channels share the handover/release interval.
A resumed Game Pak burst starts with a nonsequential access.

CPU bus boundaries poll the live enabled/ready flags in fixed channel order.
`cupid_dma_poll` captures the current CPU or callback clock only when bus
ownership permits a pending transfer to start. Timestamped peripheral requests
use `cupid_dma_kick` with their captured clock. Both paths share the same head
phase and retain arbitration at each completed pair. Polling reads the channel
flags directly; a separate runnable-state cache is unnecessary.

## Transfer clocks and cartridge accesses

Each phase uses the configured wait states for its width and region. Default
access costs include:

| Region | Halfword | Word |
| --- | ---: | ---: |
| IWRAM and I/O | 1 | 1 |
| EWRAM | 3 | 6 |
| Palette and VRAM | 1 | 2 |
| OAM | 1 | 1 |
| Game Pak ROM | WAITCNT nonsequential/sequential cost | Two halfword accesses |
| Save bus | One configured byte access | One configured byte access |

The first external access in a burst is nonsequential; later accesses are
sequential. The cartridge address counter is shared by DMA reads and writes.
Sequential strobes advance it even when a ROM write stores no data. This also
applies when the source crosses into ROM after the destination has already
started a cartridge burst. A programmed ROM access ending at a 128 KiB page
boundary makes the following ROM access nonsequential. This delayed edge
follows the programmed address even for fixed and decrementing transfers.
Nonsequential strobes latch the programmed cartridge address. The DMA source pointer
retains its programmed increment, decrement or fixed mode. Sequential ROM
reads obtain their data from the separate cartridge counter. A decrementing
source can therefore leave ROM and read OAM even though earlier ROM data
advanced through that counter. A fixed source retains its DMA address while
successive sequential ROM strobes return successive cartridge data. A fixed
source at the final transfer unit of a page therefore requests a new address
latch on every subsequent access. A fixed source one unit earlier can stream
across a data-counter boundary without that programmed-address page edge.

The two `gba.dma-pages-*` groups require 480 complete transactions and 1,920
destination publications. Explicit address/strobe tables cover incrementing,
decrementing and fixed sources, both widths, all three cartridge wait banks,
two wait-control profiles, four clock anchors and bulk/single-clock dispatch.
Every write is checked before and at its required deadline, with complete
payloads, retained source modes, completion clocks and IRQ visibility. Both
groups fail against the preceding DMA implementation. Data-address assertions
follow the address-latch/counter model; the source cartridges below directly
qualify the covered timing comparisons rather than every electrical data edge.

## Registers, latches and requests

Source and destination masks differ by channel. Enabling latches aligned
addresses and the count. A zero count means 0x4000 units for channels 0–2 or
0x10000 units for channel 3. Register writes during an enabled interval do not
replace those address/count latches. Repeat reloads the programmed count;
destination reload uses the current destination register at burst completion.
Immediate transfers disable after completion even if repeat is set.

Every channel retains its own 32-bit read latch. A valid halfword read
duplicates the halfword into both lanes. Sources below 0x02000000 retain the
old latch. A halfword write from such a source selects the lower or upper lane
using destination bit 1. Write-only display-register reads expose the CPU
prefetch value or the latest DMA read latch according to bus ownership.

Channels 1 and 2 force FIFO transfers to four words with a fixed destination.
The programmed width and destination-control bits remain readable. The FIFO
supplies a request independently of the destination address. Sound DMA can
therefore retain a RAM destination or an address whose writes are ignored.
Such a burst does not fill the FIFO; later timer overflows can request another
burst. FIFO A always drives DMA1 and FIFO B always drives DMA2, including
crossed FIFO-port destinations and repeated channel reconfiguration.
Source/count progress, bus ownership and completion signals still run. Repeated
requests during a pending or active burst are coalesced. Disabling a channel
cancels its pending request and removes it from arbitration. The final destination
write accepts a completion interrupt when the channel's IRQ bit is enabled.
Its signal reaches the interrupt controller one clock later; the existing
register pipeline makes IF visible on the following clock. Disabling or
reconfiguring the channel after that write cannot cancel the accepted signal.
A DMA reset cancels outstanding completion events. Pending IF changes retain
their individual acceptance deadlines; an earlier timer update cannot publish
a later DMA bit.

The `gba.dma-completion` group checks 196 complete scenes across all four
channels, both transfer widths, enabled/disabled completion interrupts and four
clock epochs. It preserves destination, address/count and bus-release deadlines
while distinguishing the completion signal, IF acceptance, HALT availability
and CPU IRQ input. Other scenes cover register acknowledgements on either side
of the signal, IE/IME changes, an independent timer interrupt, channel handover,
control changes and reset. Bulk and single-clock dispatch must agree. This
group and the eight existing groups with exact completion-edge assertions
reject the preceding completion model.

`gba.fifo-dma-destinations` checks 512 scenes and 1,024 complete bursts using
the default DMA1/FIFO A and DMA2/FIFO B associations. Both timers, programmed
widths, all four destination modes and four clock epochs retain the forced
four-word, fixed-destination transfer. The cases cover IWRAM, EWRAM and two
ignored BIOS addresses, including each destination publication, source/count
progress, BIOS write protection, unchanged FIFO storage and completion IF.
The preceding core rejects the first request. Four `gba.fifo-routing-*` groups
add 11,520 selector, reconfiguration, simultaneous-burst and pending-request
cases. They cover both channels writing one FIFO, crossing destinations,
RAM and protected addresses, with per-word progress, priority, overflow,
playback and completion IF checks. See [FIFO coverage](audio/fifo-power.md).
Physical request propagation and control writes coincident with activation
remain outside this qualification.

HBlank requests run on visible lines and VBlank requests run on entry to line
160. The current display-request model services channel 3 at HBlank on lines
2–161 and disables it at line 162.

## Evidence and remaining limits

`CUPID_DMA_PROBE_DIR` registers nine unchanged local DMA cartridges and repeats
them with `CUPID_TEST_BIOS`. Each run requires its original pre-display and
terminal results. All nine pass with both firmware paths, including the seven
built-in pause/preemption cases repaired by the startup display phase. Their
inputs, comparisons and guards remain intact.
The fixed-ROM case retains its original low-byte timer comparison of `0x30`.
The independent CPU/DMA page cartridge now passes all eighteen original rows
with either BIOS, including both decrement rows requiring 70 clocks. These
source results cover their recorded DMA cases. The separate ordinary HALT/DMA
interaction passes its original `0x74` return address and `0x87` timer
comparisons with both firmware paths. The CPU/bus stack also retains the
HALT/OAM boundary described in
[CPU/DMA/IRQ interactions](cpu/dma-irq-interactions.md). Passing the direct DMA
probes does not establish those remaining interaction phases.

Four checksum-pinned hardware ROMs pass ten assertions with both BIOS paths:
immediate timer sampling at 20 clocks, the two 88-clock burst-restart cases,
four channel/open-bus latch cases, and three cartridge-crossover results
including its 41-clock duration. Native regressions check individual phase
boundaries, priority, preemption, CPU stalls and internal overlap, swap locking,
masks, maximum counts, stepping modes, repeat reloads, cancellation, FIFO
requests, timed DMA I/O and clock wrap. Late dispatch is compared with advancing
one clock at a time.

The locally built transfer suite passes 1244/1244 results with no logged
failures on both BIOS paths. Its source tests and expected values are unchanged;
the private harness records results outside the tested buffers. These data
checks cover immediate and blanking transfers across several memory regions.
They do not measure every arbitration or display-request edge.

The basic display-request phase is covered by the status/IRQ/DMA ROM described
in [display timing](video-timing.md). Frame polarity, configuration changes at its window
edges, Game Pak DRQ, reserved source-control mode 3, and same-cycle IF
assertion/acknowledgement ordering still need hardware evidence. Source-control
mode 3 currently uses a fixed source. The
[Game Pak prefetch queue](prefetch.md) now shares CPU/DMA bus clocks; PPU
contention timing remains incomplete. EEPROM serial paths remain available, but
their full width/size/peripheral behavior needs separate cartridge tests.
The optional CPU/DMA page-boundary cartridge now passes the ROM-to-OAM
decrement case at its original 63-clock timer comparison on both BIOS paths.
The two other decrement page rows also pass in the current candidate. Thirty-two native
cases check fixed sources in all three ROM wait-state windows and decrementing
ROM-to-OAM transfers, both widths and four clock anchors. They require the
programmed pointer, complete destination data and exact completion clock.
The word ROM-to-OAM transaction takes 29 clocks; the preceding source-pointer
model takes 34. Its unchanged source file fails the owned negative control.
The cartridge counter's page strobes remain incompletely qualified; see
[the recorded results](prefetch.md#validation).
See [testing](../development/testing.md) for the reproducible checks.
