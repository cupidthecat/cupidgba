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
Timer periods, mixer deadlines and DMA bus-phase durations retain their
existing values.

## Access widths

Each accepted byte, halfword or word port store appends one storage word. A
byte replaces its addressed eight-bit lane; a halfword replaces its addressed
sixteen-bit lane. Other lanes retain the next write slot's contents. The
selected behavior uses that storage slot directly, rather than combining two
separate halfword stores into one word.

A complete 32-bit bus request appends once. The memory dispatcher retains its
request width through I/O publication, so the ordinary two-halfword register
dispatch cannot enqueue that word twice. Existing CPU and DMA bus waits still
own the publication deadline. FIFO reads retain their open-bus behavior.

FIFO reset control bits discard queued words while retaining the current
staged bytes and held output. A full machine reset clears all of that state.
The selected lane and reset policies have logical regression coverage;
physical propagation, overflow contents and partial-write behavior remain
unmeasured.

On each timer overflow, the FIFO samples queued storage before an empty stage
takes its next word. Three or fewer queued words request a refill. In particular,
four queued words and an empty stage do not request DMA until the next overflow.
An occupied stage keeps its current bytes and does not consume another word.
The audio request reaches DMA three clocks after the timer overflow; ordinary
DMA triggers retain their two-clock activation path. A pending request keeps
its first deadline. Bus ownership still waits for the active CPU transfer.
DMA transfers four words, retains its programmed source progress and publishes
each destination write after the existing source and destination bus waits.

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

The three `gba.fifo-refill-*` groups add 1,248 complete cases. They cover all
eight queued-word levels and four reachable staged-byte counts, both FIFOs,
both sound DMA channels, both timers and four clock anchors. Other cases
retain each of four DMA publications, signed sample order, completion IRQs,
an independent running timer, bulk versus single-clock dispatch, master power,
timer selection, disabled or non-FIFO timing and destination ownership.
The level and chronology groups fail with the preceding audio implementation;
the inactive-source controls pass with either implementation.

The chronology fixture starts with four queued words and a four-clock timer.
Its first overflow at clock 6 loads the stage without requesting DMA. The next
overflow at clock 10 requests activation at clock 13. With an idle bus and an
internal word source, destination writes occur at clocks 16, 18, 20 and 22;
the completion signal and bus release belong to clock 23, followed by IF
visibility at clock 24. Single-clock and overdue dispatch must produce identical
FIFO, timer and interrupt state.
These deadlines replace the earlier after-load decision and two-clock audio
request together. Changing either alone fails an original cartridge comparison.

Four `gba.fifo-lanes-*` groups require 256 complete access-width scenes across
both FIFOs and four clock epochs, including counter crossings. Golden queued
words and signed output-byte sequences cover all byte lanes, both halfwords,
full words, occupied stages and seven-word mixed streams. The 112 ARM/Thumb
pairs retain ordinary I/O request clocks. Twenty-four DMA cases retain all
32 paired transfer/IRQ observations, source progress and completion state.
Each group keeps a ten-second deadline and rejects the preceding implementation.

Sound-driver and music-player playback checks retain complete PCM signatures,
sample timestamps/counts, state images, DMA source positions and held samples.
Their frame spins finish complete CPU instructions, so elapsed frame endpoints
include the current FIFO/DMA scheduling phase. Sixty separate exact-duration
checks retain each following VSync call independently of that spin-loop phase.
Both firmware paths must satisfy the same observations and original run guards.

The queue and activation correction changes which three-clock idle instruction
finishes each playback frame spin. Exact elapsed endpoint fixtures include that
instruction and retain the following firmware call's separate duration check.
Paired qualification preserves ten complete timestamped PCM files and 76 complete
RAM snapshots byte for byte across the preceding and corrected cores. Endpoint
changes are at most two clocks. PCM signatures, counts, work-area images, FIFO
source positions, held samples and the 60 independent finish-call checks retain
their original expectations. The separate one-clock timer fixture checks the
three-clock request, all four DMA writes, IRQ publication, bus release and a
cancelled request beyond its activation deadline.

`CUPID_FIFO_PROBE_DIR` enables nine original local FIFO/DMA cartridges.
`CUPID_TEST_BIOS` repeats them with an external image. The manifest pins each
input digest and its original result-display and terminal addresses. The runner
requires a passing result at both boundaries; display code cannot conceal an
earlier failure by overwriting the result register. No cartridge or BIOS image
is distributed.

The manifest also retains all 26 encoded comparisons across the nine bundled
images and separate source image. Qualification observes actual instruction
issue after IRQ arbitration, checks the pinned opcode and requires the measured
register to equal the original immediate or literal. Register-to-register
comparisons also require the live expected-value register. Missing, reordered
or failed comparisons reject a zero-result terminal. The two disable inputs
without encoded comparisons retain their original result boundaries.

Owned negative controls deliberately bypass failure writes while leaving zero
at both boundaries. They must fail when the measured value is wrong, the live
expected operand differs or a required comparison never issues. Separate
controls reject changed opcodes, register selection, immediates and literals.
The observation callback leaves emulated state and clocks unchanged.

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

The IRQ cartridge retains its original `0x008000AF` comparison with both live
operands and both firmware paths. The combined queue and activation ordering
also satisfies all eight original prefetch comparisons and both threshold
comparisons. All 20 configured FIFO cases retain their inputs and run guards;
16 pass. The DMA completion signal follows the final destination write by one
clock, before the existing IF register pipeline. That separate event repairs
both IRQ-latch runs at their original `0x7B` comparison. Base and the separate
source base still fail on both firmware paths. Their first timer reading
remains `0x62` instead of `0x68`. These are remaining failures, not new expectations.
Every original input, comparison and execution guard remains required. The
power, refill and access-width regressions cover their selected logical model;
complete FIFO/DMA timing acceptance remains open in #76. Physical queue capacity,
overflow contents, lane latching, reset pipeline behavior and timer/DMA/IRQ
edges remain unmeasured. See [DMA](../dma.md) and [audio timing](timing.md).
