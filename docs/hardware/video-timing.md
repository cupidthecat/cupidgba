# Display status and requests

Each scanline lasts 1232 clocks and each frame has 228 lines. Visible lines are
0 through 159. The HBlank flag asserts 1007 clocks after line start; the next
line starts 225 clocks later. VCOUNT advances at that boundary, while its match
flag updates one clock later. VBlank asserts on line 160 and clears on line 227.

Flags and requests use separate deadlines. The following offsets are measured
from the relevant line transition or HBlank edge:

| Event | Flag or register change | IRQ request | IF visibility |
| --- | --- | --- | --- |
| HBlank entry | 0 | 1 | 2 |
| VBlank entry | 0 | 1 | 2 |
| VCOUNT match | 1 after line start | 2 after line start | 3 after line start |

The IRQ unit handles the later availability and CPU-line stages. See
[interrupt timing](interrupts.md). Scheduled callbacks retain their original
timestamps, so a late dispatch preserves the relative ordering of these stages,
timer overflows and DMA bus accesses.

DISPSTAT writes preserve the three read-only flags and update its IRQ enables
and target line. Bits 6 and 7 are unused. The register layout is described in
the [programming manual, page 27](https://www.manuallib.com/download/2023-10-17/AGB%20Programming%20Manual.pdf#page=27).
A target write evaluates the current VCOUNT immediately. A newly matching
target can request an IRQ; retaining an already matching flag does not create
another edge. Changing the target does not acknowledge a queued request.

HBlank DMA requests occur on visible lines and VBlank requests at entry to
line 160. Display DMA uses a mode latch during the blanking interval. Its active
window requests transfers three clocks after the transitions to lines 2 through
161 and disables the channel at line 162. The DMA engine then applies its normal
activation, handover and read/write phases. See [DMA](dma.md).

## Validation and limits

An unchanged hardware ROM checks twelve observations through its four existing
menus: five DISPSTAT edges, VCOUNT advancement, three IF edges and three DMA
timer samples. Both BIOS paths pass all twelve original expectations. The
observer selects each menu with deterministic button presses, verifies the
input digest and rejects failed or incomplete console results. It does not
modify the ROM or its measured values.

Native tests check individual flag/IF deadlines, target writes and masks,
blanking/frame edges, the display DMA latch and request window, and equivalence
between late dispatch and advancing one clock at a time across clock wrap.
The complete bus, BIOS math, DMA and timer/IRQ suites retain their results.

Display mode changes at latch/window edges, frame polarity, DRQ and same-cycle
request/acknowledgement priority need further hardware captures.
Reset and BIOS-skipped display phases also lack hardware captures.
Pixel fetches, bus contention, register effects and forced-blank transitions
are described in [the pixel pipeline](pixel-pipeline.md), including its remaining
qualification limits. The status and request results alone do not establish
complete PPU accuracy.

## Completed display images

`cupid_video_set_frame_callback` observes the existing line-160 frame handoff
after register mirrors update. It supplies borrowed RGBA pixels and the
console-clock deadline, including during late event dispatch. A caller copies
the pixels to retain the completed image. The callback runs on the emulation
thread and must not change the core or advance time. Install or clear it while
paused; its host ownership survives console reset.

The frontend retains that complete image while the next frame draws and
during STOP. Frame pacing and the RTC interval still consume their existing
clocks. Publication changes no scanline, IRQ, DMA or audio deadline. Native
checks cover every pixel, a partially drawn next-frame control, counter and
clock wrap, STOP, late dispatch, reset and observer removal.
