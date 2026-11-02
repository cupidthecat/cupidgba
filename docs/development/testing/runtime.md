# Execution, frontend and runner checks

[Testing overview](../testing.md). Run commands from the repository root.

## Result-boundary controls

`runner.terminal-*` checks actual result-boundary issue and completed idle
branches, including IRQ-discarded prefetch and retained HALT fetches. See
[runner result boundaries](../runner-boundaries.md) for the protocol, negative
controls and unchanged cartridge execution guards.

`runner.observation-cycles` checks repeated instruction sequences without
accepting later failures after a minimum count. `runner.cyclic-display-protocol`
rejects changed store operands, fabricated counts and incomplete output. The
[original cyclic display check](../../hardware/render/cyclic-controls.md)
exercises these observations through HALT and display interrupts; its pixels
remain unqualified.

## Frontend input

The `platform.input-*` groups run real SDL2 virtual-controller state through
the frontend event poll. They cover bindings, mixed source ownership, strict
stick boundaries, focus/hotplug and keypad IRQ/STOP interactions. Every group
has a ten-second deadline. See [input handling](../../hardware/input.md) for the
complete observations and scope.

## Audio threading

`platform.audio` checks interpolation and a live SDL2 dummy device with a
concurrent consumer, including a producer slower than playback. The optional
host output sink observes each playback buffer and its source-backed prefix.
The live checks require every source-backed frame to retain the expected PCM;
the slower case must also exercise underrun. The final faded sample is not a
fixed-value observation. Linux CI also runs this check in a separate ThreadSanitizer
build. To reproduce it locally:

```sh
cmake -S . -B build-thread -DCMAKE_C_COMPILER=clang \
  -DCMAKE_BUILD_TYPE=Debug -DCUPID_THREAD_SANITIZER=ON
cmake --build build-thread --target test_audio_output --parallel 2
ctest --test-dir build-thread -R '^platform.audio$' --output-on-failure
```

ThreadSanitizer and AddressSanitizer require separate build directories.

## Cooperative contexts

`platform.context` checks shared host ownership and local data across repeated
cooperative yields, normal completion and cancellation. AddressSanitizer builds also require each live
local buffer's boundary to remain poisoned after every return to its stack.
The portable backend uses the sanitizer fiber interface and clears the generic
stack-cleanup marker installed by `makecontext`; otherwise `swapcontext` can
erase redzones belonging to suspended frames. CI runs this test with the native
backend, the portable AddressSanitizer backend and ThreadSanitizer. To check the
portable backend locally:

```sh
cmake -S . -B build-context-portable -DCMAKE_C_COMPILER=clang \
  -DCMAKE_BUILD_TYPE=Debug -DCUPID_SANITIZERS=ON -DCUPID_FAST_CONTEXT=OFF
cmake --build build-context-portable --target test_context test_execution --parallel 2
ctest --test-dir build-context-portable -R '^platform.context$|^gba.execution-' --output-on-failure
```

## Execution boundaries

Seven `gba.execution-*` groups cover frames with no eligible wake, paced interval
accounting, one-time instruction issue and stores after wake, reset and stack
cancellation, shared standalone/cable ownership, and an already eligible wake.
ARM and Thumb cases include scheduler wrap. Each group keeps a five-second
deadline. The IRQ acceptance regression checks the write at clock two, the
following CPU phase at clock three, and the original handler registers and
return addresses at clocks eight or nine. Its former single-step observation
combined the write with an empty internal clock; the retained fetch now occupies
that clock. The cartridge operands, terminal observations and execution limits
are unchanged.

The batch group compares sixteen batched and repeated-step sequences in both
CPU modes and across scheduler wrap. It checks every observed boundary, input
and wake publication, complete registers, frame/audio counters, event queues
and final memory/images. It also checks early return, zero work, STOP and
resuming a different request after the callback has returned. The headless
runner uses these boundaries to retain its original checkpoint, terminal,
frame, step-count and elapsed-clock guards without switching host stacks
after every ordinary step. Its instruction observer and all process deadlines
remain unchanged.

## Frontend persistence

`platform.frontend-frames` checks bounded single-console runs with an owned ARM
SRAM guest. An observer includes the actual frontend entry point and retains
real core intervals, SDL input/render/audio and battery paths; it replaces only
host pacing. Counts 1, 2, 299, 300 and 301 check exact input, interval, display
and periodic/final save boundaries. STOP counts intervals without requiring a
new video frame and persists the complete seeded RTC state. The shipped
executable separately creates and reloads a complete SRAM image across three
processes. Other cases cover earlier input quit, invalid counts, the largest
accepted count and read-only destination failures. Each process has a
three-second guard and the group has a thirty-second CTest deadline. This
qualifies the frontend run bound and persistence paths, not physical chip
timing or full-game progress. See [running games](../../usage/running.md).

`platform.save-startup` uses a small generated ROM to check invalid-save startup,
explicit capacity mismatch, dotted directories and path-length handling. It
requires Python 3 and SDL2. No game, BIOS or save input is distributed. See
[save files](../../hardware/cartridge/save-files.md) for behavior and remaining limits.

## Bounded game replays

`runner.key-script-valid` checks a guest KEYINPUT read before the first frame
and frame-indexed DMA reads while another DMA holds the CPU. File and inline
inputs must produce identical execution output, complete EWRAM, completed-frame
screenshots and audio bytes. The group also checks duplicate-frame ordering,
65,536 events, maximum line/file sizes, empty scripts, numeric bases, future
events and both common line endings.

`runner.key-script-invalid` checks 26 malformed-file and conflicting-option
cases. Every case must fail before ROM loading, preserve the input bytes and
leave all seven requested output files unchanged. Both groups have ten-second
CTest deadlines and three-second process guards. See
[input replay format](../../usage/running.md#input-replays).

`platform.frontend-replay` runs owned ARM and Thumb guests through the shipped
SDL2 executable. Their complete 32 KiB SRAM images must match the headless
runner, including three button samples and a transient keypad IRQ from two
changes on the same frame. Removing the intermediate press removes that IRQ
observation. Fresh and seeded batteries, process reboots, first-console input
ownership across two, three and four linked consoles, empty scripts, and changes
while DMA holds the CPU are covered.
Malformed scripts must fail before ROM loading and preserve existing batteries
and RTC files; nonregular inputs are rejected without opening a blocking stream.
The group has a thirty-second deadline and ten-second process guards.

`platform.key-replay-*` checks ordered application, held state, frame bounds and
the ordinary keypad wake path. `platform.input-replay` and local input controls
check that physical buttons cannot overwrite scripted buttons or create an
unwanted keypad IRQ. Escape and ordinary inputs on other consoles still work.

To run another authorized input for a fixed number of frames:

```sh
./build/cupidgba-headless /path/to/game.gba --bios /path/to/gba-bios.bin \
  --frames 600 --steps 168537600 --screenshot .local-research/frame.ppm
```

## Headless save input

The headless runner uses a fresh backup unless `--load-save FILE` explicitly
loads battery bytes after reset and before guest execution. It uses the same
backup-family and capacity validation as the frontend. A failed explicit load
stops before creating output files. The input is opened for reading; no save
sidecar or automatic writeback is created. `--dump-save FILE` remains an
explicit raw backup snapshot, so persistence replays should use separate input
and output paths:

```sh
./build/cupidgba-headless /path/to/game.gba --load-save .local-research/previous.sav \
  --frames 600 --steps 168537600 --dump-save .local-research/next.sav
```

`runner.save-input` exercises guest reads and writes across separate processes,
complete backup images, capacity inference, retained Flash selection and rejected
inputs. It uses owned cartridges and the existing ten-second group deadline.

## Screenshots and instruction observations

The headless screenshot is the last completed native 240x160 image, written as a
binary PPM file. The runner counts every video presentation and can stop inside
a DMA-held instruction; it does not count the ready flag once per CPU return.
Frame-indexed input is delivered at those presentation boundaries. See
[frame boundaries and capture checks](../frame-boundaries.md) for the suspension
contract and owned regression cartridge. Step and emulated-cycle budgets bound
execution; `--expect-loop-register` reports the result register
at an ARM branch to itself. `--terminal-address` restricts that observation to
a specific instruction address.

`--expect-register-at ADDRESS REGISTER VALUE` requires an ordered issued
instruction and its live register value. `--expect-register-pair-at ADDRESS
REGISTER VALUE REGISTER VALUE` checks both operands at the same instruction.
The runner allows up to 128 ordered observations. Missing, extra, reordered
or wrong values fail even when both result boundaries contain zero. Records
include actual ARM/Thumb mode and opcode for pinned-input verification.
Observation runs after IRQ arbitration and includes failed ARM predicates;
HALT/STOP polls and discarded exception fetches do not issue instructions.

The `arm.observer-*`, `runner.observations` and `runner.fifo-comparisons`
groups check passive state/clock preservation, IRQ replacement, reset ownership,
branches, failed predicates, complete comparison operands and bypassed failure
writes. Their ten-second deadlines and owned process guards remain fixed.
The FIFO manifest preserves all 26 original comparisons, including the source
base's second `8Bh` comparison that is absent from the bundled image. See
[FIFO power](../../hardware/audio/fifo-power.md) for the retained timing failures.

`--checkpoint-address ADDRESS` additionally records the selected register at
the first ARM visit to that address. Success requires both the checkpoint and
terminal values to be zero. An unvisited checkpoint fails, even if later guest
code reports zero. `runner.checkpoint` exercises this with owned instructions.
`--key-event FRAME MASK` changes the pressed-button mask at a completed frame
boundary. Repeat it in frame order to provide a deterministic input sequence;
a zero mask releases all buttons. This uses the same key state and keypad IRQ
path as the frontend.
`--dump-vram FILE` and `--dump-io FILE` write raw observer snapshots for test
result inspection.
`--dump-ewram FILE` provides the result counters and failure log for the local
timer suite observer.
The example explicitly allows at most one instruction per emulated clock for
600 frames (600 x 280896 clocks). The runner's default 50-million-step guard
can stop a valid long run when prefetch permits more instructions per frame.
This changes the observation budget; it does not change emulation timing or
any hardware test expectation.

## Deterministic RTC input and snapshots

Headless runs keep a fresh, deterministic clock unless an RTC input is supplied.
`--rtc auto|on|off` selects the device, using the same cartridge detection as the
frontend. `--rtc-time YYYY-MM-DDTHH:MM:SS` seeds a valid calendar from 2000
through 2099. The seed is applied before guest execution, including an external
BIOS startup; that startup therefore counts as elapsed RTC time.

```sh
./build/cupidgba-headless clock-game.gba --rtc-time 2024-02-29T12:34:56 \
  --frames 600 --steps 168537600 --dump-rtc .local-research/first.rtc
./build/cupidgba-headless clock-game.gba --load-rtc .local-research/first.rtc \
  --frames 600 --steps 168537600 --dump-rtc .local-research/next.rtc
```

`--load-rtc FILE` reads the existing cartridge-bound battery format. A seed
supplied in the same command overrides its calendar and fractional second,
while retaining control and interrupt-parameter settings. Argument order does
not change that precedence. Loaded host timestamps never add offline time;
headless execution advances the clock only through emulated console and STOP
battery clocks. `--dump-rtc FILE` writes an atomic battery snapshot with a zero
host-time anchor. It reports write failures through a nonzero exit status.

These options do not imply `--rtc on`. An explicit seed, load or snapshot with
no enabled RTC fails before guest execution or output creation. Use an RTC
cartridge or select `--rtc on` for an owned fixture. Invalid files and calendar
arguments also stop startup. Runs never discover or write an adjacent `.rtc`
file implicitly. Keep input and output paths separate when preserving a seed.
The snapshot retains battery state, not the CPU, display or a partial GPIO
transaction. See [RTC battery formats](../../hardware/cartridge/rtc.md).

`runner.rtc-input` checks 62 short startup/replay trials under a ten-second
CTest deadline and three-second process guards. Owned ARM and Thumb guests
read all seven calendar bytes through GPIO, record the transfer count and
output pointer, and reach an observed, completed terminal. Checks compare the
entire EWRAM image and every battery byte, including fingerprint, fractions,
control, parameters, carry flags, host anchor and checksum. They cover all
three battery versions, both hour formats, retained invalid seconds, explicit
presence, calendar precedence, independent
process reloads, two expected STOP/frame-deadline results, rejected inputs and
failed atomic replacement. STOP snapshots must retain every elapsed battery
clock, including the existing 300-frame startup allowance in the secondary
elapsed-clock guard, without turning an incomplete video run into a pass. Existing files
must survive startup failures. `CUPID_TEST_BIOS` adds the same explicit-entry
profile with that image mapped; it does not qualify firmware startup.

## Unsigned execution arguments

Headless frame and step budgets, addresses, register indices, masks and input
values accept unsigned decimal, `0x` hexadecimal and leading-zero octal text.
Signs, leading or trailing whitespace, invalid digits and values beyond 64 bits
are rejected before ROM loading or output creation. Each option still checks its
own range. A whitespace-prefixed negative value can no longer become an enormous
unsigned step budget through the C conversion routine.

`runner.arguments-unit` checks 139 valid/invalid parser cases and preserves the
output value on failure. `runner.arguments` exercises every numeric argument
position through the real CLI, preserves all seven existing output files on
rejection, and requires completed checkpoint/terminal controls for valid radices.
The tests keep three-second subprocess and ten-second group deadlines.
