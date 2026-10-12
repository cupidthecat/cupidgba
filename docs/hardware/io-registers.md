# I/O register reads

CupidGBA applies writable masks before storing BG0/BG1 control, window layer
controls and blending controls. The same masked values reach rendering state
and later CPU reads. Sound-control readback retains routing and volume fields;
FIFO reset bits act as strobes and do not remain in the register shadow.

| Register | Stored mask |
| --- | --- |
| BG0CNT, BG1CNT | `0xDFFF` |
| WININ, WINOUT | `0x3F3F` |
| BLDCNT | `0x3FFF` |
| BLDALPHA | `0x1F1F` |
| SOUNDCNT_L | `0xFF77` |
| SOUNDCNT_H | `0x770F` |

Reads from write-only video registers, sound FIFOs, DMA source/destination
registers and unmapped I/O expose the existing bus value. Halfword and byte
reads select the addressed lanes. A word read assembles both halfwords. ARM
and Thumb use the existing CPU prefetch model; a DMA transfer's read latch
remains available until the next timed CPU access completes the handover.

Explicit zero-filled holes in the sound register block and DMA transfer counts
return zero. They remain distinct from addresses that do not drive the bus.

The pinned local register-read suite retains all 130 original expectations and
runs through either BIOS path. Native regressions add reads at all three widths
in both instruction modes, distinct bus lanes, DMA handover, masked subsystem
state, zero holes and a byte write following FIFO reset. Input checksums,
terminal R12, complete snapshots, all per-case results and absence of logged
failures are required. These runs qualify the source expectations; they do not
provide a new hardware capture.

This coverage does not establish every I/O edge or complete audio behavior.
PSG trigger writes, wave RAM banking and playback, FIFO partial writes, sound
power transitions, waveform precision and host audio synchronization remain
part of the broader audit in [issue 1](https://github.com/cupidthecat/cupidgba/issues/1).
