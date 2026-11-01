# Audio checks

[Testing overview](../testing.md). Run commands from the repository root.

## FIFO power, refill and storage

The five `gba.fifo-power-*` groups cover 160 owned observations of ignored
sound-off writes, ARM/Thumb stores, complete DMA transfers, power edges during
DMA and queued-word disposal. Each keeps a ten-second deadline. The optional
`CUPID_FIFO_PROBE_DIR` points to a local directory containing the nine pinned
FIFO/DMA cartridges. `CUPID_TEST_BIOS` repeats all nine with that image. Both the
original pre-display result, ordered comparisons and terminal must pass. Known
FIFO timing failures remain enabled. See [FIFO power](../../hardware/audio/fifo-power.md).

The three `gba.fifo-refill-*` groups retain 1,248 complete level, chronology and
selection cases under ten-second deadlines. Timer consumption tests queued
storage for a DMA refill before loading an empty stage. Two groups reject
the preceding order; inactive-source controls pass with either order. The
unchanged original IRQ cartridge now reaches its `0x008000AF` comparison and
passes both result boundaries on each BIOS path. Other FIFO timing gaps remain.

Four `gba.fifo-lanes-*` groups cover 256 complete access-width scenes with
ten-second deadlines. They retain every queued word and signed output byte,
occupied-stage behavior, ARM/Thumb I/O request clocks, 32-clock DMA/IRQ histories
and mixed-width output order across both FIFOs and counter crossings. All four
reject the preceding lower-port latching behavior. Original FIFO cartridges,
comparisons and execution guards remain unchanged.

Four `gba.fifo-queue-*` groups cover 3,472 queue rotation, overflow, reset and
DMA scenes under ten-second deadlines. Seven distinct queued words retain
their individual storage lanes through wrap. A full queue differs from an empty
queue even when its pointers match. Each group rejects the preceding storage
model. Checks preserve every output byte, staged-byte phase and affected DMA
publication/completion clock. See [FIFO capacity](../../hardware/audio/fifo-power.md#capacity-and-overflow).

## Playback and source reconstruction

Playback checks keep exact elapsed-frame observations alongside sixty exact
VSync-call duration checks. A frame spin may finish one or two clocks past its
deadline because a CPU instruction must finish; FIFO/DMA arbitration determines
that phase. The call-duration checks independently retain firmware execution
costs. Complete PCM/state signatures, sample counts, source addresses, held
samples and all existing loop budgets remain checked with both firmware paths.

`CUPID_FIFO_SOURCE_ROM` adds the separately rebuilt base cartridge with either
BIOS path. The original source includes a second timer comparison missing from
the bundled image; both inputs remain registered independently. Reproduce it
with `tests/roms/build_fifo_source.py` and a local FASMARM executable as described
in the FIFO guide. The builder checks every source/include byte apart from line
endings, verifies the complete output digest and refuses to replace source files
or a different existing output. Both profiles retain their 360-frame,
150-million-step, 90-second process and 100-second CTest bounds.

`runner.fifo-source-builder` checks isolated assembly, verified publication
and preservation of existing inputs after compiler or checksum failures.

## Original audio cartridge

The optional local audio cartridge observer selects ten unchanged signal tests:

```sh
python3 tests/roms/check_audio.py --runner build/cupidgba-headless \
  --rom /path/to/audio-test.gba
```

Use `--test tone`, `stereo`, `sweep`, `steps`, `dynamic`, `noise`, `wave`, `bias`,
`fifo` or `silence` to run one selection. `CUPID_AUDIO_TEST_ROM` registers each
selection separately in CTest. The observer requires the pinned digest, complete
execution and capture timestamps, and the original source-defined signals.
Software frequency sequences must complete in their declared order and spacing.
Wave and polynomial checks require the complete pattern up to an unknown phase.
The FIFO selection checks every signed source table level, stereo routing and
timer hold changes. It does not establish the complete FIFO/DMA waveform.
Existing instruction limits apply; each selection retains a 120-second process
deadline and a 130-second CTest deadline. The private source revision and build
recipe stay with the research checkout; the ROM is not committed.

## Timestamped mixer captures

The headless runner's `--dump-audio FILE` exports every mixer capture through a
sample sink. It begins with the eight ASCII bytes `CPDPCM01`, followed by packed
little-endian records containing a 64-bit CPU clock and signed 16-bit left and
right values (`<Qhh`, 12 bytes). The sink records frames independently of host
queue overflow. It fails the run on file write or close errors. See
[audio registers](../../hardware/audio/registers.md) and
[audio timing](../../hardware/audio/timing.md) for coverage and qualification limits.
