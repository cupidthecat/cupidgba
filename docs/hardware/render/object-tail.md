# Object texture-tail ownership

The HBlank-free object engine releases its texture port at object clock 960.
Attribute fetching keeps its existing 964-clock deadline. These are clocks
relative to the object engine's start, which follows the line boundary by
40 clocks. A texture fetch at clock 958 still occupies the CPU-ready edge at
959. The following texture edges are free; the attribute port can remain busy.
Normal line budgeting is unchanged.

The existing per-line budget latch selects this behavior. Changing the live
HBlank-free control bit after that latch does not resize the current line's
budget. The vertical mosaic counter retains its later line-boundary update.
Ending texture requests does not reset the object buffer, retime OAM, or add
special handling to CPU or DMA code. Both bus owners use the same port-ownership
query as before.

## Original cartridge check

The pinned texture-tail cartridge reads object VRAM near that boundary, then
compares its timer result with 78h. The preceding core reads 79h. With the two
fetch deadlines separated, both BIOS paths pass the unchanged comparison at
its original issued Thumb instruction, the pre-display result and the completed
terminal. The cartridge's bytes, expected immediate, frame/step limits and
process timeout remain unchanged.

Fetch it with `tests/roms/check_object_tail_probe.py --fetch --directory PATH`,
then configure `CUPID_OBJECT_TAIL_PROBE_DIR=PATH`. An authorized `CUPID_TEST_BIOS`
adds the external-firmware run. Linux and Windows CI run the built-in-firmware
case and preserve the downloaded license notice.

## Native controls

Seven groups exercise 4,096 scenes:

- 256 port/pixel scenes use fourteen overlapping 64-pixel objects and a visible
  final object. A long tail reaches the texture deadline; a short tail leaves
  attribute scanning active after texture work finishes. Complete working-line
  pixels and both port calendars are checked.
- 128 affine scenes retain the complete pre-cutoff pixel state and reject late
  texture fetches while preserving normal-mode progress.
- Four CPU groups total 3,456 byte, halfword and word reads, with each starting clock
  from 956 through 964. They require the correct number of accepted beats,
  unchanged data and the final internal load cycle.
- 256 DMA histories retain destination publication, source/destination progress,
  completion interrupt timing, payload and bus release across all four channels.

The scenes cover tile and bitmap display modes, both texture depths, live
HBlank-free changes after latching, and four clock epochs crossing signed and
unsigned counter boundaries. Every group rejects the preceding core. Shared
immediate-comparison checks reject altered opcodes/immediates, wrong zero values,
missing or reordered observations, and incomplete result boundaries.

The original cartridge qualifies the listed CPU boundary. The larger native
matrices check the corresponding logical renderer and DMA model; they are not
new physical captures of every affine, control-write or DMA phase. The pixel
checks preserve direct fetch-to-pixel output as the selected compatibility
policy. They do not establish whether physical hardware drains pending pixels
after its final texture request. Wider display accuracy requirements remain
open. The [normal-mode counterpart](display-startup.md) retains its original
8Eh timer comparison through both firmware paths.
