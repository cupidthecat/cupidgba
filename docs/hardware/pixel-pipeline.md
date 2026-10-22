# Pixel rendering and display memory

CupidGBA advances rendering with elapsed clocks. Register and memory writes
synchronize the engine before changing state. Completed pixels retain their
colors, and fetched texels retain their values until later stages consume them.
The status, IRQ and DMA deadlines remain those in [display timing](video-timing.md).

## Fetch and output stages

Text backgrounds fetch map entries and tile halfwords into a shift register.
Affine backgrounds fetch a map byte followed by a texel. Bitmap backgrounds
fetch a pixel every four clocks. These fetches hold indices or direct colors;
palette reads occur during layer selection or blending.

Affine map fetches use a 16-bit byte address. A map that extends beyond the
end of background VRAM wraps before the map byte is read, and the wrapped
fetch occupies its ordinary bus slot. This selected affine-address policy
is separate from text-map and tile addresses: their out-of-range reads keep
the existing background bus latch and do not occupy a fetch slot.

The merge unit selects the nearest two visible layers. Lower BG numbers win
equal BG priorities; OBJ wins a tie with BG. An intervening opaque layer blocks
alpha blending with a deeper target. Semi-transparent OBJ can force alpha
blending with an eligible second layer. Brightness requires the first target
and the window's effects enable, and is applied once.

The color model normalizes red and blue to five bits and retains six green
bits through the arithmetic stage. Input bit 15 supplies green's low bit;
the final output reduces green to the displayed five bits. Coefficients 17
through 31 saturate at 16. Alpha rounds the combined intensity before channel
saturation. Brightness rounds the final increased or decreased intensity,
so red and blue have the same precision. The
[programming manual, page 83](https://www.manuallib.com/download/2023-10-17/AGB%20Programming%20Manual.pdf#page=83)
uses a maximum of 63 for green in brightness arithmetic.

The implemented bitmap pipeline exposes the following stages for the first pair:

| Stage | Elapsed line clock |
| --- | --- |
| First bitmap texel fetched | 35 |
| First pixel selected and palette read, when needed | 47 |
| First pixel's effects completed | 49 |
| Both pixels emitted | 53 |

Subsequent pairs leave at eight-clock intervals. Green swapping exchanges only
the green components of an adjacent pair after effects. A write can affect later
pairs without changing those already emitted. Output uses the existing BGR555
to RGB presentation mapping; these checks do not model LCD color response.

Horizontal and vertical windows use comparison flags. Window controls determine
which layers and effects participate at merge time. BG and OBJ mosaic counters
retain their phase across fetch and merge stages. OBJ pixels are prepared for
the next line by a separate attribute, matrix and texture fetch engine.
HBlank-free mode shortens the OBJ fetch budget; the vertical mosaic counter
still advances at the line boundary and resets at the existing frame edge.
Changing the budget does not restart the mosaic group or extend memory fetches.

CupidGBA decodes OBJ shape 3 as an 8-by-8 texture for all four size fields.
This selected logical policy uses the ordinary attribute, matrix and texture
fetch stages, including their bus ownership and fetch budget. Affine double
size enlarges the bounding box around that texture. OBJ mode 3 remains
suppressed before matrix or texture fetching. These choices have software
regressions; physical behavior for the prohibited encodings remains unmeasured.

Display control passes through three line latches sampled at clock 40. BG
fetching requires both the current and latched enable. Forced blank remains
active while either the current or latched blank bit is set. Affine reference
writes reload the next line; PA and PC update the running fetch coordinates.
PB and PD advance the line reference at clock 1232, so HBlank writes can affect
that step. Frame start reloads the programmed references.

## Bus ownership

CPU and DMA accesses wait for the fetch unit that owns the addressed bank.
Palette and VRAM word transfers use two halfword beats. OAM uses a single beat
at either width. Each write beat becomes visible separately. CPU transactions
retain bus ownership while waiting, and DMA retains its current read or write
phase. Cartridge prefetch and DMA handover continue through their existing paths.

In bitmap modes, addresses `0x06018000` through `0x0601BFFF` cannot access the
bank assigned to background memory. Reads return zero and writes are discarded.
The upper mirror still maps to `0x06014000`; the normal lower bitmap bank remains
accessible. Opcode fetches refresh their VRAM mapping when crossing these banks.

## Qualification

The affine-map address group checks 132 scenes across both affine backgrounds,
both overflow-bit settings, all four map sizes and three clock epochs. Fixed
map-byte locations cover both sides of the 64-KiB carry. Each scene requires
the map/texel CPU stall, retained texel and access timestamp, and all 240 pixels
of the complete output line. It qualifies the selected address policy;
physical behavior at these map boundaries remains unmeasured.

Native regressions cover modes 0 through 5, frame selection, clipping, text
scroll/flip/palette banks, affine coordinate and reference latches, normal and
affine OBJ, OBJ windows, rectangular windows, mosaic, effects, forced blank,
per-beat CPU/DMA writes and late dispatch across clock wrap.

Four OBJ mosaic groups check normal and identity-affine sprites with both
color depths and fetch budgets. They cover all sixteen horizontal and vertical
sizes at an unaligned screen position and require all 4,915,200 pixels across
128 complete screens, including the backdrop. Phase checks retain the existing
size-change wrap and frame reset, require OAM/VRAM fetches to stop at the shorter
budget, and compare changing budgets under bulk and single-clock dispatch at
three clock epochs. All four groups fail against the preceding implementation:
its shortened fetch loop also stopped vertical mosaic progression. These checks
qualify this fetch-budget interaction; physical counter edges and reset phases
remain outside those checks.

Eight object-shape groups check 768 paired ordinary/shape-3 scenes, every
pixel in 1,536 complete screens and 946,176 paired bus clocks. They cover all
size fields, both color depths and mapping modes, both fetch budgets, normal
flips, identity-affine and double-size bounds, semi-transparency, OBJ windows,
mode-3 suppression and clock wrap. Both members of every pair satisfy the
analytic image, including transparent texels and the complete backdrop.
Six groups reject the preceding shape-skip path; the two mode-3 groups retain
its existing suppression. These checks qualify the selected decoding and
its fetch timing without claiming physical measurements of those encodings.

The unchanged VRAM-mirror ROM passes all ten original register and CRC
expectations with either BIOS path. Ten selections of the unchanged access
probe each record 1231 stall observations in SRAM. Their maps require these
occupied CPU bus clocks within the probed line:

| Selection | Occupied clocks | Count |
| --- | --- | --- |
| Bitmap mode 3 | 35 through 1003, every four clocks | 243 |
| One 4-bit text BG, horizontal scroll zero | Tile bases 32 through 992, every 32 clocks; offsets 0, 4, 20 | 93 |
| One 8-bit text BG, horizontal scroll zero | Same tile bases; offsets 0, 4, 12, 20, 28 | 155 |
| Affine BG2 | 34 through 1006, clocks congruent to 2 or 3 modulo 4 | 487 |
| Affine BG2 and BG3 | Every clock from 32 through 1006 | 975 |
| One palette lookup | 47 through 1003, every four clocks | 240 |
| Direct bitmap color, no effects | None | 0 |
| Direct bitmap color with backdrop alpha | 49 through 1005, every four clocks | 240 |
| Three normal objects, OAM | 41, 43, 45, 59, 61, 75, 77; then 91 through 337, every two clocks | 131 |
| Three normal objects, texture VRAM | 45 through 91, every two clocks | 24 |

The emitted IRQ probe's fixed lead adds 138 to these clocks to locate the SRAM
observations. Palette, OAM and OBJ clocks include their one-clock acceptance
phase. Text fetches extend into HBlank for the final tile. The object scene uses
three 16-by-16, 4-bit objects and disables the remaining entries; OAM scanning
continues after their texture fetches finish. All 12,310 observations match the
source-derived maps and a pinned diagnostic run. This qualification does not
include a newly captured hardware trace.

Three unchanged raster ROMs have explicit pixel checks:

- Green swap checks both stable regions of each line, without assigning an
  unmeasured transition edge.
- Affine line stepping checks the complete gray bitmap. HBlank DMA first writes
  PD at clock 1012 and then every two clocks; the line step precedes the write
  at 1232 and therefore uses PD 109. Row `y` samples bitmap row `floor(y*109/256)`.
- Sprite mosaic checks unchanged texture regions and aligned groups of widths
  two and eight in the original scenes.

Input digests, complete snapshots and subprocess success are required. Supplied
photographs and scaled images provide visual comparisons, not raw framebuffer
hashes. The complete CPU/bus, BIOS math, DMA, timer/IRQ and status suites remain
required alongside these checks.

Reference writes retain independently written axes and become the next line's
affine origin. PA/PC writes affect future fetch steps; PB/PD writes participate
in the line step at its actual deadline. Display-enable latches materialize
earlier fetch clocks before publishing their new value. These are the selected
logical rules. They are implemented without retroactive pixel fetches.

Eight raster groups retain 396 owned scenes: eleven reference, matrix and
display operations at eighteen clock positions with two texture periods.
They check complete expected geometry and bulk/one-clock dispatch equality
across 792 frames and 30,412,800 monochrome endpoint pixels. The asymmetric
period-seven texture prevents a two-pixel shift from hiding in a checkerboard.
Expected frames include the first activation pixels, writes around clock 40,
fetch/merge boundaries, HBlank and line end. These are geometry and logical
phase checks; channel expansion remains covered by the separate color tests.

Broader BG reference-DMA sequences and the exact physical column in the supplied
photographs remain unmeasured. Color regressions check fractional vectors, all raw-color
endpoints, coefficient saturation and red/blue symmetry. Scanout tests retain
the extra green bit through direct bitmap, background palette, backdrop and
semi-transparent OBJ inputs. These qualify the implemented numerical model;
direct hardware captures for fractional rounding and bit 15 remain open.
Physical mosaic sampling edges and reset or BIOS-skipped phases remain
unmeasured. Horizontal and vertical size writes have the logical qualification
described below. The ten selections do not qualify every
scroll phase, layer combination, OBJ mode or contention pattern. These limits remain under
[issue 16](https://github.com/cupidthecat/cupidgba/issues/16) and the complete
PPU audit under [issue 1](https://github.com/cupidthecat/cupidgba/issues/1).

## Horizontal mosaic writes

A MOSAIC write changes the selected horizontal width without clearing the
elapsed background or OBJ group counter. Each output pixel increments its
counter; equality with the selected width starts the next group. If a write
shortens the width past the current count, that terminal count has already
passed. The group continues through the remaining line. Both horizontal
counters reset at the next line start. Vertical mosaic retains its separate
four-bit line counters and fetch latches.

Layer selection, palette reads, transparency and OBJ priority retain their
ordinary phases. Keeping a group does not freeze the whole renderer or defer
register and memory writes. This is the selected logical counter policy;
these software observations do not supply a physical capture of a mid-line
register strobe.

Thirty-two `gba.mosaic-horizontal-*` groups cover all sixteen starting and
replacement widths at twenty-three clock positions for backgrounds and opaque
sprites. The 11,776 scenes use an owned asymmetric one-hot texture. Fixed
expected geometry checks every pixel of the written line and the following
line, with both bulk and one-clock dispatch: 47,104 complete scanlines and
11,304,960 pixel comparisons per profile. Counter observations also distinguish
a retained group from an artificial wrap at sixteen. The following line must
match the stable replacement width after both counters reset.

The initial Windows controls compile the same fixture against the preceding
core with its matching headers. Both width-one groups pass; all thirty groups
that can shorten an active period reject the preceding retained-counter state.
Existing raster, color, window, object, blanking and bus checks remain required.
The geometry corpus does not qualify LCD color response, vertical mosaic
sampling on hardware, every sprite-priority interaction, or physical reset phase.

## Vertical mosaic writes

Background and OBJ vertical mosaic retain separate four-bit line counters.
Changing the selected height keeps the elapsed counter. Equality with the new
height ends a group; a counter that has passed that height continues until its
four-bit wrap. Affine backgrounds retain their separate reference accumulators
and apply the selected group height when a group ends. Background fetches and
next-line OBJ preparation can therefore observe different sides of a boundary
write. Shortening the OBJ fetch budget for HBlank access retains its separate
line-counter progression.

The 64 `gba.mosaic-vertical-*` groups cover text and bitmap backgrounds, normal
and identity-transformed OBJ, all sixteen counter phases and replacement
heights, and byte, halfword and word writes at three adjacent boundary clocks.
Owned Gray-code textures encode each row in eight thirty-pixel stripes. The
expected data describe every pixel in each complete observed line, including
opaque black. Each case checks the stored control and both final counters.
Bulk and one-clock dispatch use the same geometry. Additional cases retain
constant-height OBJ grouping with HBlank access and place writes at signed and
unsigned clock wrap.

The private comparison executes the independent pinned renderer, scheduler and
register bus for all 9,216 ordinary-budget scenes. All complete geometry and
counter/control records agree across 400,896 scanlines and 96,215,040 geometry
pixels per core. The separate boundary survey agrees in 98,304 background
states. Its shortened-budget OBJ results differ, so those results are not used
as a universal timing replacement. The retained independent line-counter policy
also matches static screen-row grouping; its constant-height controls remain
separate from the ordinary-budget source comparison.

The fixture configures owned graphics during VBlank and enters the ordinary
line-zero callbacks. It does not choose a power-on LCD phase. These are logical
timing and geometry observations. Physical sampling, arbitrary OBJ matrices,
every priority/window interaction and full PPU accuracy remain open under
issues 16 and 1.

## OBJ texture-bank boundary

OBJ texture addresses wrap within their 32 KiB bank after both the tile offset
and byte offset have been added. In one-dimensional 8-bit mode, an odd tile
base can place the lower four rows of a tile beyond the bank end. Those bytes
come from the beginning of the OBJ bank. Masking only the tile number left
those rows transparent. Two-dimensional 8-bit mapping still ignores the low
tile bit. Bitmap modes retain their existing restriction on the lower bank
after the address wraps.

Sixteen `gba.object-bank-*` groups cover all eight display-mode values, both
color depths and tile mappings, the first and last two tile bases, normal
flips, identity-affine and double-size identity-affine sprites, clipped and
wrapped origins, one-clock dispatch and clock wrap. The twelve owned texture
patterns describe every pixel of each eight-by-eight source tile. Each scene
checks the complete framebuffer, including transparent pixels and backdrop.

The independent stable-renderer comparison covers 288 complete frames across
all twelve sprite sizes. The address correction repairs 24 end-tile cases and
retains all 264 controls. The preceding core fails six native groups at pixel
assertions; bitmap restrictions remain positive controls. These results select
the logical bank-address policy. They do not measure physical memory-port
edges or qualify every renderer interaction.

## Host work between observable clocks

Display-memory accesses still advance each physical beat and synchronize the
renderer before its data strobe. A palette or VRAM word publishes its low
halfword before its high halfword. The shared store helper preserves the
address decoder, write restrictions and separate strobes.

When a non-final beat has no pending CPU-line stage, peripheral deadline or
clock observer in either of the next two clocks, the bridge tests its first
ready predicate directly after the ordinary one-clock tick. Its owned ready
event remains queued for the second clock. An occupied display port continues
through ordinary per-clock polling. Callbacks, deferred event queues, linked
clock coordinators and host clock observers retain the general path.

An already blank pixel with background fetches and OBJ disabled can advance
through an interval without repeating layer selection. Short intervals visit
the original union of comparator, output, OBJ-marker and line-boundary stages.
Longer intervals fill identical white pairs between window boundaries while
retaining horizontal latches, vertical and horizontal mosaic counters, OBJ
elapsed markers and affine line stepping. A register write synchronizes the
preceding interval before changing these conditions. Unsupported wrapping
intervals retain the general path. White output uses the existing five-bit
channel conversion, including its maximum channel value of 248.

The blank-scanout regression compares complete pipeline, background, scanline
and framebuffer state in 2,976 scenes. It covers every mosaic width and phase,
shortened periods, window boundaries, retained latches, clipped intervals,
later layer activation and clock-wrap fallbacks. Another 1,280 scenes compare
ordinary observed clocks with eligible quiet clocks, including partial word
visibility and the final CPU-ready edge. The earlier `fb55cab` checkpoint also
compared both groups with freshly compiled preceding cores and matching
headers. Those archived comparisons apply to their recorded horizontal-counter
policy. Current blank-scanout checks compare complete bulk and one-clock state
under the retained-counter policy; the fixed mosaic geometry above supplies
its separate before/current qualification.
At the `da40d4c` checkpoint, the local unoptimized save-reboot check reached its
original deadline. That receipt remains preserved. The broader PPU acceptance
limits above remain open.

## Direct Sound DMA interaction

The same bus timeline serves display fetches and Direct Sound DMA. A FIFO
refill request becomes eligible three clocks after its timer overflow; the
DMA engine then arbitrates at an available bus boundary. Each destination
strobe still waits for its selected memory port, including occupied display
slots. FIFO mode forces four words and a fixed destination, including when
the programmed destination is outside the FIFO ports.

The final write retains its own data deadline. DMA completion signals on the
following clock, and the interrupt register accepts that signal one clock
later. These stages do not move pixel fetches or release a busy display port
early. See [Direct Sound storage and power](audio/fifo-power.md) and
[DMA phases](dma.md) for the request, storage and completion rules.

## CPU-driven palette writes

`gba.cpu-raster-half` and `gba.cpu-raster-word` execute owned ARM programs from
cartridge memory. Each program enables the VBlank interrupt source, masks CPU
interrupt entry, acknowledges IF and calls Halt through the replacement BIOS.
The VBlank wake anchors each frame before a VCOUNT polling loop writes a new
backdrop color during visible output. ROM code cannot write HALTCNT directly;
the BIOS call supplies that register strobe through the normal CPU path.

Each group checks 36 scenes: scanlines 1, 79, 80 and 159, three color
transitions, two instruction delays and a constant-color control. Halfword
and word stores use the ordinary palette bus. The checks require guest
progress, the programmed VCOUNT, masked CPU interrupts, unchanged preceding
lines, the new color on following lines and identical final two frames.
The changed line must contain an ordered transition, and seven additional
NOP instructions must move that transition later. Pixel comparisons use
RGB555 values so channel expansion does not affect the result.

Run both tests with `ctest --test-dir build -R '^gba.cpu-raster-' --output-on-failure`.
For an optional local BIOS check, pass an authorized BIOS path as the third
argument to `test_cpu_raster half` or `test_cpu_raster word`. CI uses the
replacement BIOS and needs no external firmware. These tests cover the
recorded CPU, BIOS wait, interrupt wake and palette interaction; they do not
qualify physical pixel timing or every raster and display-memory conflict.
