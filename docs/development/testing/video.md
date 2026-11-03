# Video checks

The `gba.object-output-ports` and `gba.object-output-frames` groups separate
texture-port ownership from pixel submission. The first requires 6,144 complete
final request cases, including unchanged pixel metadata on the closing request.
The second executes sixteen owned guest scenes and checks 614,400 final-image
pixels after their completion marker. Both retain ten-second deadlines.
Existing object-tail CPU, DMA and port checks keep their original data and
timing assertions. See [object texture-tail ownership](../../hardware/render/object-tail.md)
for the selected output model and hardware qualification limits.

[Testing overview](../testing.md). Run commands from the repository root.

## Display startup

The [display startup checks](../../hardware/render/display-startup.md) require the
original scanline, VBlank, IRQ/HALT and normal texture-tail comparisons. They
also verify which register the program uses to report its result. Configure
`CUPID_DISPLAY_PROBE_DIR` and run `rom.display-*` for these five cartridges.

## Object texture tail

The [object texture-tail checks](../../hardware/render/object-tail.md) cover
HBlank-free texture-port release separately from attribute fetching. Run
`gba.object-tail-*` for the native renderer/CPU/DMA groups. The optional
`rom.object-tail*` cases retain the original cartridge comparison and both
result boundaries.

## Vertical mosaic

The vertical mosaic geometry matrix can be selected with:

```sh
ctest --test-dir build -R '^gba[.]mosaic-vertical-' --output-on-failure
```

Its 64 groups partition four layer types and sixteen starting phases. Each
group retains all sixteen replacement heights, three access widths and three
adjacent boundary clocks, with bulk and one-clock dispatch. The owned row
fixtures check every pixel in each completed scanline. OBJ controls retain
constant-height grouping with HBlank access; additional writes cross signed
and unsigned clock wrap. Each group has a ten-second limit.

## Object bank boundaries

OBJ bank-boundary coverage runs with:

```sh
ctest --test-dir build -R '^gba[.]object-bank-' --output-on-failure
```

The sixteen groups partition eight display-mode values and normal/affine
sprites. They check 2,928 complete frames and 112,435,200 pixels per profile,
including bank-end reads, bitmap restrictions, flips, clipping, wrapped
origins and signed/unsigned clock wrap. Each group has a thirty-second limit.
The expected source-tile rows come from independently executed observations.

## Reserved object shapes

Eight `gba.object-shape-*` groups check the selected shape-3 decoding through
complete images and CPU-visible display-bus ownership. They include normal,
affine, alpha and OBJ-window scenes and retain mode-3 suppression. Each group
has a thirty-second deadline. See [pixel rendering](../../hardware/pixel-pipeline.md)
for the exact policy, coverage and physical qualification limits.

## Original display status cartridge

The display status ROM has four interactive menus. Its observer selects each
with button events and requires all twelve original status, VCOUNT, IF and DMA
observations:

```sh
python3 tests/roms/check_ppu.py --fetch --directory .local-research/test-roms
python3 tests/roms/check_ppu.py --directory .local-research/test-roms \
  --runner build/cupidgba-headless --test status
```

The other cases are `vcount`, `irq` and `dma`. Omit `--test` to run all four;
add `--bios` for the external path. Their frame budgets include the original
cycle sweeps and, with an external BIOS, startup. The default instruction limit
is unchanged. `CUPID_TEST_ROM_DIR` registers all four checks, and `CUPID_TEST_BIOS`
adds the external comparisons. See [display timing](../../hardware/video-timing.md).

## Renderer and access timing

The native renderer tests advance the actual fetch and merge pipeline. They
cover all six display modes, OBJ, windows, mosaic, effects, affine and display
latches, writes between stages, CPU/DMA bus beats, blanking and clock wrap.
The four `gba.object-mosaic-*` groups check complete normal/affine screens,
all mosaic sizes, both OBJ color depths and HBlank fetch budgets. They also
require line-counter progression after fetching stops, size-change/frame phases
and identical bulk/single-clock results while the budget changes. Run them with
`ctest --test-dir build -R '^gba.object-mosaic-' --output-on-failure`.
The optional raster and access observers use unchanged inputs and source-derived
expectations rather than hashes of hardware photographs:

```sh
python3 tests/roms/check_raster.py --fetch --directory .local-research/test-roms
python3 tests/roms/check_ppu_access.py --fetch --directory .local-research/test-roms
python3 tests/roms/check_raster.py --directory .local-research/test-roms \
  --runner build/cupidgba-headless
python3 tests/roms/check_ppu_access.py --directory .local-research/test-roms \
  --runner build/cupidgba-headless
```

The access observer runs ten menu selections: bitmap, 4-bit and 8-bit text,
single and paired affine backgrounds, three palette cases, and sprite OAM and
texture VRAM. Each selection requires all 1231 SRAM observations, including
the expected occupied slots. Use `--test bitmap`, `--test text-4`,
`--test text-8`, `--test affine-2`, `--test affine-both`,
`--test palette-single`, `--test palette-direct`, `--test palette-alpha`,
`--test object-oam` or `--test object-vram` to select one case. CTest registers
each separately. Each 1500-frame budget covers the original per-frame samples;
the default step limit and per-case timeout remain unchanged. `--dump-save`
exports the runner's save-memory snapshot and fails if no save data exists.
The three raster cases check 20,592 green-swap pixels, 38,400 affine-step pixels
and 2,816 sprite/mosaic pixels. `--bios` repeats these raster checks through the
external path. See [pixel rendering](../../hardware/pixel-pipeline.md) for the
timing derivation and the remaining hardware qualification boundaries.
The native video group also checks complete frame publication across clock
and counter wrap, late dispatch, STOP, reset and observer removal. A partially
drawn next frame is its negative control: every published pixel must retain
the preceding complete image.

## Color precision

Color checks cover fractional numerical vectors, all 65,536 input-color
endpoints, coefficient saturation and matching red/blue precision. Scanout
checks retain the extra green bit through direct and palette fetches, backdrop
alpha and semi-transparent OBJ effects. Physical fractional rounding and bit 15
remain unqualified.
