# Direct Sound FIFO power

`SOUNDCNT_X` bit 7 gates writes to both Direct Sound FIFOs. Byte, halfword
and word stores while master sound is off do not enqueue samples. This also
applies to DMA destination strobes. An accepted DMA still reads its source,
advances its address and count, releases the bus and publishes its completion
interrupt. Sound power does not cancel the transfer.

A transition from enabled to disabled discards queued words in both channels.
Writing the enabled value again preserves the queue. The current implementation
keeps the staged word and held output separate from the queued storage. Its
phase through a power transition remains a hardware qualification limit.
The change does not alter the existing timer, mixer or DMA clock model.

## Regression coverage

The five `gba.fifo-power-*` groups require 160 owned observations:

- 56 ignored writes across both FIFOs, all access widths and byte lanes, at
  four scheduler clock anchors.
- 28 decoded ARM and Thumb byte, halfword and word stores while sound is off.
- 32 complete immediate DMA transfers across both DMA channels, both FIFO
  destinations, either power state and four clock anchors.
- 32 DMA transfers that change power after the first destination strobe,
  checking all remaining payload words, source progress and completion IRQs.
- 12 power cycles through byte, halfword and word control writes, checking
  discarded old words, fresh signed-byte playback and repeated enable writes.

The clock anchors include signed and unsigned 32-bit wrap and a later epoch.
Each group keeps its ten-second deadline. All five groups fail against the
preceding core without changing their assertions.

`CUPID_FIFO_PROBE_DIR` enables nine original local FIFO/DMA cartridges.
`CUPID_TEST_BIOS` repeats them with an external image. The manifest pins each
input digest and its original result-display and terminal addresses. The runner
requires a passing result at both boundaries; display code cannot conceal an
earlier failure by overwriting the result register. No cartridge or BIOS image
is distributed.

The bundled base image differs from its accompanying assembly source. Its
second timer-read path loads the expectation into the measured register and
branches on the preceding comparison flags. Its data contains `0x00800098`,
but that path never compares the second timer reading. The source instead
loads `0x0080008B` into the expected-value register and compares both readings,
requiring `0x00800068` and `0x0080008B`. The bundled input remains unchanged;
its terminal cannot qualify that absent second comparison.

`CUPID_FIFO_SOURCE_ROM` enables the additional source-built image, with a
separate digest, result-display address and terminal. FASMARM 1.44 reproduces
its 2,524 bytes from the pinned source, compared with 2,520 bundled bytes.
The eight other FIFO inputs, nineteen available prefetch sources and nine DMA
sources reproduce their bundled images byte for byte. The legacy prefetch
boundary input has no corresponding source in the pinned checkout.

Build the source variant into a separate private directory:

```sh
python tests/roms/build_fifo_source.py --directory /path/to/local/source \
  --assembler /path/to/fasmarm --output /path/to/private/fifo-source.gba
```

The builder verifies all six source/include files, ignoring only carriage
returns, and requires the complete expected output digest. It works from an
isolated copy and refuses an output inside the source checkout or an existing
file with different contents. Configure `CUPID_FIFO_SOURCE_ROM` with that output
path alongside the original `CUPID_FIFO_PROBE_DIR`. Both BIOS selections retain
the same frame, instruction and process guards. The source case remains a
timing failure; the current model reads `0x62` at its first `0x68` comparison.

The current main-based candidate passes six of eighteen cartridge runs.
The base, prefetch, IRQ, threshold, IRQ-latch and one disable group still fail
on both BIOS paths. Original inputs, comparisons and budgets are retained,
and all failures remain registered when the profile is enabled. The base
cartridge still fails in its second timing case after the power correction.
These results do not qualify the complete FIFO/DMA timing model. Queue capacity,
overflow behavior, powered-on partial writes, staged-word reset, exact refill
thresholds and timer/DMA/IRQ phases need further work. See
[DMA](../dma.md) and [audio timing](timing.md).
