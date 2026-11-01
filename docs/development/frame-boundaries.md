# Headless frame boundaries

`--frames N` counts completed video presentations. A DMA transfer can keep one
CPU instruction waiting across several VBlanks, so the runner counts the video
callback rather than sampling the boolean `frameReady` flag after each step.
It stops at presentation N, even when that instruction has not returned.

Frame-indexed input uses the same boundary. Before resuming a held instruction,
the runner applies each event due at the completed frame and checks keypad IRQs.
An event at frame zero applies before execution. The frame limit takes priority
over an input event at that same final frame, as it does over terminal acceptance.

## Capturing a complete image

`--screenshot` writes a native 240x160 binary PPM. The runner copies the output
inside each presentation callback and writes the last completed image. This
matters when a terminal is reached midway through a visible frame: the live
buffer can already contain rows from the next frame. If execution stops before
any presentation, the screenshot contains the initial cleared output.

RAM, I/O, savedata and audio exports still describe execution at the stopping
point. They are not rolled back to the screenshot's presentation. A screenshot
and an early-terminal RAM snapshot can therefore have different timestamps.

## Suspending a DMA-held instruction

A scheduled peripheral callback can call `cupid_gba_yield_steps` during a local
`cupid_gba_run_steps` request. The host regains control without completing or
counting the interrupted step. A subsequent execution call resumes the same
callback, bus wait and instruction. Reset or destruction cancels the retained
stack instead; the callback's context must stay alive until either outcome.

The yield helper returns true after resumption. Calls outside a scheduled event,
from external execution, or during single-step, frame and paced-interval requests
return false without suspending. In particular, it does not shorten an interval
or underflow its overshoot accounting. The ordinary step-boundary callback keeps
its existing contract and does not count a peripheral yield as a returned step.

Reset disables timed CPU accesses before refilling the reset vector. Otherwise,
a cancelled DMA-held fetch could enter its old bus wait during the refill, before
the platform reset clears DMA and IRQ-clock ownership.

## Regression checks

`runner.frame-boundaries` creates a 444-byte owned ARM cartridge with no external
assembler dependency. It fills a mode-4 screen, changes its palette with VBlank
DMA, samples keypad input with another channel and holds the CPU with a full
EWRAM word fill. Two complete frames occur within one instruction return.
The checks require:

- Green at frame two and blue at frame three, exactly 280,896 clocks apart.
- Rejection of an issued but unfinished zero-result terminal at frame two.
- Acceptance with a three-frame budget, retaining the complete green image
  while the next live image is partly blue.
- An unchanged one-step limit and a cleared image before any presentation.
- Frame-indexed input reaching VBlank DMA reads while the CPU remains held.

The preceding runner fails these observations: it reports one frame at the late
terminal and captures the third image when asked for two frames. Original ROM
expectations and runner budgets are not changed to accommodate that behavior.

`gba.execution-yield-*` compares suspended and uninterrupted DMA execution in
ARM and Thumb modes, including scheduler and frame-counter wrap. It checks
registers, prefetch, IRQ/input state, DMA progress, RAM, complete video output,
queued events and exact-once instruction issue. Other groups check rejected
yield requests, twelve transitions to other execution requests, and
reset/destruction while a callback is suspended. Three independently compiled
runner mutations fail when frame suspension, immutable capture or timely input
is removed. Native checks reject two core mutations: retaining a timed reset fetch and
allowing a yield outside a scheduled event.

These are execution-boundary and capture guarantees. They do not establish
pixel-pipeline accuracy or turn a bounded boot replay into a full-game test.
Earlier comparisons made with the coalescing counter retain their original
results; their reported frame counts are not treated as exact physical counts.

## CPU probe acceptance

The IRQ, saved-status, user-bank and generated-corpus runners share
`tests/support/probe_run.h`. They sample the video frame counter at each
ordinary CPU return, including its 32-bit wrap, and reject expired frame or
clock limits before accepting a result. The IRQ, saved-status and user-bank
probes allow the final ordinary step to complete the terminal. The corpus
driver also enforces its launcher's existing strict 150-million-step ceiling.

A terminal must issue through the instruction observer and return to its ARM
self-branch pipeline with no unfinished instruction. A HALT-held refill can
resume without inventing another issue observation. The user-bank observer
also runs at actual issue, while retaining its interrupt-disabled requirement
and any preceding observer installed by its caller. Original comparisons,
register checks and input bytes are unchanged.

These probe limits are acceptance checks at ordinary CPU return boundaries.
A long DMA transfer can carry that return past a deadline; the result then
fails. Use the headless presentation callback when execution must stop at the
frame edge itself. The diagnostic `terminal=yes` field in the IRQ, saved-status
and corpus tools still records issue; the process status and complete result
checks determine success.

`runner.probe-boundary-*` covers 1,024 predicate combinations, 3,584 actual
terminal returns, 72 DMA-held deadline cases, eight HALT/wake pairs and eight
unwoken deadline cases. It also checks zero budgets and accumulated return
counts after resumption. The DMA cases complete two presentations in one
ordinary return, including frame-counter and scheduler wrap.

`runner.probe-adapters` compiles the four real entry points with tighter frame
limits for owned inputs. Each must accept a complete terminal, reject an issued
terminal whose DMA wait exceeds the frame budget, and reject a wrong expected
result. The owned transport corpus visits all 10,000 records and all comparison
sites; it tests the driver contract, not arithmetic accuracy. The pinned
arithmetic corpora remain separate. Removing any caller's completion gate
makes its owned late-terminal case pass incorrectly. Ten shared-guard mutations
and all four caller-gate mutations are rejected by these tests. A separate
corpus control rejects completion at its step ceiling. Three observer-ownership
mutations reject lost chaining or restoration of the prior callback and context.
