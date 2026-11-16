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

### Multiply carry

ARM and Thumb multiplication now derive C from the multiplier's internal
carry-save state, using operands sampled before destination writes. The six
regression groups cover 15,705 instruction executions, including 72 recorded
hardware carry observations, arithmetic and flag preservation, register
overlap, following conditions and DMA chronology. See
[multiply flags](../hardware/cpu/multiply.md) for the model and its limits.

### SST Flash command recovery

The SST device cancels pending erase setup after an invalid fourth, fifth or
sixth command write, allowing a fresh unlock sequence to start another command.
Five regression groups cover 396,026 rejected writes and recovery scenes,
including ARM/Thumb stores, DMA readback, clock wrap and nominal ready edges.
See [Flash programming](../hardware/cartridge/programming.md). These checks
qualify the digital command model without measuring physical abort timing.

### Backup allocation recovery

Backup initialization and type changes stage new storage before releasing the
attached device. Allocation failure preserves its bytes, decoder, profile and
pending programming event. Flash growth can retry without losing its bank or
command; a failed EEPROM transfer discards its remaining bits so they cannot
be interpreted with the wrong address width. Runtime Flash detection retains
its recognized family while storage is unavailable.

Seven fault-injection groups cover 740 scenes through production allocation,
configuration, CPU memory and DMA paths across clock wrap. They require complete
state and byte preservation, unchanged ready deadlines and successful retries. These are
host recovery guarantees; see [save files](../hardware/cartridge/save-files.md)
and [cartridge tests](testing/cartridge.md) for their scope.

### Game Pak opcode bounds and backup waits

Instruction fetches outside the mapped ROM image now sample address-derived
halfwords. This covers branches beyond the image and sequential fetches past
its end. Crossing a 32 MiB window selects the next window's image bytes and
wait states. Device opcode callbacks retain priority, and image replacement
refreshes the direct view before another instruction fetch.

Both backup windows start with the same SRAM wait setting after reset. CPU
accesses and DMA transfers through the `0x0F000000` mirror therefore retain
the same timing as `0x0E000000`, including before the first `WAITCNT` write.
Nine regression groups cover opcode values and execution, mapped-image
lifetime, reset defaults, all SRAM selectors and DMA completion/IRQ edges.
See [the Game Pak bus](../hardware/rom-bus.md) and [memory tests](testing/memory.md).
These checks preserve the selected digital model; EEPROM instruction-fetch
signaling and physical cartridge bus transitions remain unqualified.

### Mapped instruction-region transitions

Sequential execution and the second fetch of a branch refill now select the
addressed mapped device when they cross a region boundary. This corrects stale
RAM/display pointers and destination waits, including internal-memory aliases
and VRAM banks. Branch history and already sampled pipeline words are retained;
device opcode samples still take priority. Seven regression groups cover the
mapped transitions, guest register effects, IRQ sampling and DMA handover. See
[instruction fetch mapping](../hardware/memory/opcode-fetch.md).

This does not qualify opcode values or physical fetch timing in unmapped,
I/O or save-memory regions, and it does not resolve the timing conflicts below.

### JOY transmit register readback

Guest reads of the JOY transmit registers now return their staged data in JOY
mode, including after a peer has consumed the word. Reads preserve send-ready,
command flags and the retained receive word. CPU and DMA reads use the existing
I/O lane and input-bus paths. Console reset clears the registers; a peer reset
command only sets its event flag. See [JOY commands](../hardware/serial/joybus.md)
for the documented register contract and the limits of the command interface.

### GBA display-control mask

DISPCNT writes retain the GBA control fields and keep the protected legacy-mode
bit clear. Five regression groups cover 3,704 register, ARM/Thumb, DMA, input
and display scenes, including 48 paired rendering comparisons. They preserve
raw DMA payloads, neighboring registers and the existing bus timeline. BIOS
activation of the legacy processor remains outside the supported machine;
see [I/O register behavior](../hardware/io-registers.md) for that policy.

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
  EEPROM framing, chip-specific programming behavior and peripheral signal
  timing remain incompletely qualified. Image replacement
  now owns fresh backup storage and cancels the old device's events; see
  [image ownership](../hardware/cartridge/image-mapping.md).
- The [card-reader model](../hardware/cartridge/card-reader.md) supports queued
  strip input, serial camera control, scanline/DMA transfers and selected IRQ
  timing. Continuous acquisition, overrun behavior and retail workflows remain
  unqualified; camera type 2 and unused serial-register combination mirrors are
  not implemented.
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
