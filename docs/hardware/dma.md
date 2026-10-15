# DMA and bus ownership

DMA requests become ready two clocks after the trigger. Once the CPU releases
the bus, the engine spends one handover clock, then schedules each read and
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
started a cartridge burst. Crossing a 128 KiB cartridge-counter boundary
requires nonsequential timing in the current model. The DMA source pointer
retains its programmed increment, decrement or fixed mode. Sequential ROM
reads obtain their data from the separate cartridge counter. A decrementing
source can therefore leave ROM and read OAM even though earlier ROM data
advanced through that counter. A fixed source retains its DMA address while
successive ROM strobes return successive cartridge data.

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
The programmed width and destination-control bits remain readable. Repeated
requests during a pending or active burst are coalesced. Disabling a channel
cancels its pending request and removes it from arbitration. A completion IRQ
is requested after the final write. Pending IF changes retain their individual
acceptance deadlines; an earlier timer update cannot publish a later DMA bit.

HBlank requests run on visible lines and VBlank requests run on entry to line
160. The current display-request model services channel 3 at HBlank on lines
2–161 and disables it at line 162.

## Evidence and remaining limits

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
Two other decrement page rows still fail on each path. Thirty-two native
cases check fixed sources in all three ROM wait-state windows and decrementing
ROM-to-OAM transfers, both widths and four clock anchors. They require the
programmed pointer, complete destination data and exact completion clock.
The word ROM-to-OAM transaction takes 29 clocks; the preceding source-pointer
model takes 34. Its unchanged source file fails the owned negative control.
The cartridge counter's page strobes remain incompletely qualified; see
[the recorded results](prefetch.md#validation).
See [testing](../development/testing.md) for the reproducible checks.
