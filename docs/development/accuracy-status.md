# Accuracy status

CupidGBA has a timed CPU, DMA, audio and display pipeline. That is not a claim
that every hardware feature or every game is correct. This page separates
implemented subsystems, reproducible conflicts and work that still needs
coverage. The overall acceptance tracker is [issue 1](https://github.com/cupidthecat/cupidgba/issues/1).

## Delivered subsystem work

The integrated pipeline reached main in
[PR 17](https://github.com/cupidthecat/cupidgba/pull/17). Its merge tree passed
Linux GCC, Linux Clang sanitizers and Windows checks. Historical issue updates
that describe those branches as separate drafts refer to earlier revisions.

### Timed rendering and display-memory access

The renderer advances text, affine and bitmap fetches, next-line object work,
palette selection, effects and output with elapsed clocks. Register and memory
writes reach their corresponding fetch/latch stages. CPU and DMA requests
arbitrate against the same palette, VRAM and OAM ports. This replaces the former
one-snapshot-per-scanline renderer and fixed display-memory wait estimates.

Coverage includes all six display modes, sprites, windows, blending, mosaic,
green swapping, forced blank, affine/display latches, visible/blanking edges,
late dispatch and clock wrap. Original status, raster, display-access and
object-tail inputs retain their expected results and run limits. Exact pixel
comparisons use source-derived expectations; photographs and visually aligned
captures are not treated as framebuffer hashes. See
[video tests](testing/video.md), [pixel rendering](../hardware/pixel-pipeline.md)
and [display timing](../hardware/video-timing.md).

This implements the rendering/contention work scoped by
[issue 16](https://github.com/cupidthecat/cupidgba/issues/16). Complete display
accuracy remains part of issue 1. Physical pixel-pipeline drain behavior and
sampling phases are not established by these software tests.

### Direct Sound storage, power and DMA requests

Both FIFOs gate byte, halfword and word writes while master sound is disabled.
A falling power edge discards queued storage; repeated enable writes retain it.
Queued words, the playback stage and the held output are separate. Accepted
DMA keeps its transfer progress, bus release and completion interrupt even
when sound power changes.

Regression groups cover queue wrap/overflow, narrow lanes, stage retention,
refill order, fixed request-source selection, destination changes, pending
request cancellation, re-enable and simultaneous bursts. All original input
comparisons, display results, terminals and budgets remain required, including
known failures. See [audio tests](testing/audio.md) and
[FIFO behavior](../hardware/audio/fifo-power.md).

This implements the storage/power and routing work scoped by
[issue 76](https://github.com/cupidthecat/cupidgba/issues/76). It does not resolve
the remaining whole-system FIFO timer conflict below or qualify physical
analog output and reset propagation.

## Reproducible conflicts

- The bundled and separately rebuilt FIFO base inputs still report `62h`
  against their original `68h` timer comparison with either BIOS path. The
  full FIFO selection remains 16/20. The four failures stay registered when
  the optional inputs are configured; they are not converted into expected
  passes or hidden by a changed terminal observer.
- One external-BIOS branch/IRQ case still reports `F4h` against `F5h`. A later
  zero terminal result does not override that failed issued comparison.
  See [CPU/DMA/IRQ observations](../hardware/cpu/dma-irq-interactions.md).
- Broader actual-unoptimized input runs have retained original deadline
  failures. Their logs remain distinct from the passing hosted native scope.
  Retrying a case does not erase the first result or justify raising its limit.

The first two conflicts remain in the CPU/bus and DMA interaction work under
[issue 10](https://github.com/cupidthecat/cupidgba/issues/10),
[issue 8](https://github.com/cupidthecat/cupidgba/issues/8) and issue 1. Earlier
IWRAM, OAM and odd-address signed-halfword failures are not current failures:
the original twelve bus runs now pass with both firmware paths. No original
expected value changed.

## Remaining implementation and coverage

- CPU input feedback and bus-latch coverage are not complete for every byte,
  word, partial-lane and instruction interaction. The complete 2020-observation
  timing matrix and the original prefetch cases do not exhaust those states.
- [Internal memory remapping](../hardware/memory/internal-control.md) now uses
  a shared CPU/DMA decoder, retained BIOS protection and live opcode rebinding.
  Its selected device timing and same-edge behavior still need independent
  hardware qualification. Invalid EWRAM timing values retain the existing
  compatibility policy without simulating physical corruption or lockup.
- DMA still needs wider display-window edge, external Game Pak request and
  same-edge interaction coverage. Reserved source mode 3 uses a fixed source.
- Ordinary cartridge images support up to 32 MiB; the documented
  [paged controller](../hardware/cartridge/paged-rom.md) supports up to 64 MiB.
  Card-reader hardware is absent. EEPROM framing, chip-specific programming behavior and
  peripheral signal timing remain incompletely qualified. Image replacement
  now owns fresh backup storage and cancels the old device's events; see
  [image ownership](../hardware/cartridge/image-mapping.md).
- PSG, FIFO and digital mixer tests do not calibrate PWM or physical audio
  output. Link/JOY peer models likewise do not establish electrical behavior.
- Bounded game boots, scripted scenes and generated-battery replays are useful
  regressions, not full-game acceptance. Sustained gameplay and ordinary
  frontend save workflows need broader coverage.

Where hardware is unavailable, use the best-supported documented software
behavior, preserve conflicting observations and test the selected policy.
Unmeasured physical behavior is a stated limit, not a reason to invent a
passing result or an arbitrary timing adjustment. Close a scoped issue only
when its implementation and verification requirements are met; keep broader
subsystem acceptance in issue 1 until it is demonstrated.
