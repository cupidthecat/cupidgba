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
qualify this fetch-budget interaction; they do not add physical counter-edge
captures or establish the remaining reset and prohibited-OBJ behavior.

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

The exact BG reference-DMA column and the first few pixels after display-latch
changes still differ between diagnostic implementations and need targeted
hardware qualification. Color regressions check fractional vectors, all raw-color
endpoints, coefficient saturation and red/blue symmetry. Scanout tests retain
the extra green bit through direct bitmap, background palette, backdrop and
semi-transparent OBJ inputs. These qualify the implemented numerical model;
direct hardware captures for fractional rounding and bit 15 remain open.
Prohibited OBJ shapes/modes, precise mosaic changes at counter boundaries, and reset or
BIOS-skipped phases also remain open. The ten selections do not qualify every
scroll phase, layer combination, OBJ mode or contention pattern. These limits remain under
[issue 16](https://github.com/cupidthecat/cupidgba/issues/16) and the complete
PPU audit under [issue 1](https://github.com/cupidthecat/cupidgba/issues/1).
