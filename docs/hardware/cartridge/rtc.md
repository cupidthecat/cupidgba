# Cartridge GPIO and RTC

CupidGBA detects an RTC driver signature in the cartridge image. `--rtc on`
enables the device explicitly, `--rtc off` disables it, and `--rtc auto` uses
signature detection and known board wiring. The default is automatic. Detection
does not patch CPU execution or change cartridge bytes. The separate
[solar and rumble devices](peripherals.md), [gyroscope](gyroscope.md) and
save-bus [X/Y sensor](tilt.md) have their own configuration and input.

The data, direction and read-control registers occupy physical cartridge
offsets `0xC4`, `0xC6` and `0xC8`. Data and direction use four bits; a direction
bit of one selects output. Reads combine the output latch with device input.
Setting control bit 0 enables register reads. With reads disabled, the original
cartridge bytes remain visible. The three ROM wait-state windows select the
same physical port; addresses in the upper 16 MiB of each window do not.
See the [GPIO register description](https://problemkaputt.de/gbatek-gba-cart-i-o-port-gpio.htm).

CPU and DMA word data accesses, and ARM opcode fetches that start a Game Pak
access, use two halfword strobes. The first port is
sampled or written before the second strobe, with scheduler events running
between them. DMA uses the cartridge burst counter to select the physical
port. Byte stores currently leave the port unchanged. That byte policy and
the alias decoder still need direct hardware qualification.

The RTC accepts control, date, time and interrupt-parameter transactions over clock, serial-data
and chip-select pins. Command and payload bits are sampled on rising clock
edges. Commands use data latched while the clock is low; a simultaneous data
change on a payload's rising edge can only clear that bit. Outgoing data
advances on falling edges. Reads capture a response when the command completes
and repeat that response while chip select remains high. Driving data low
during a read suppresses the previously output shift bit. It corrupts the
response snapshot, while the underlying calendar/control register stays intact.

Chip-select release aborts a transaction. Invalid command prefixes discard
their byte and allow another aligned command without chip-select release.
Status writes commit at the eighth rising edge and can accept more bytes in
the same transaction. The first bit after each completed status byte also
publishes the shifted byte; an incomplete continuation retains that value.
Date and time writes commit after a complete frame; their partial, overrun and
active-output behavior still needs qualification.

Interrupt parameters use a live sixteen-bit shift register. Each accepted
payload bit shifts the word right and enters at bit 15, so frequency output
can change before all sixteen bits arrive. The nominal parameter-read command
also receives input and leaves serial output released. A host that releases
the data line therefore reads `0xffff` and writes that value into the register.
The model retains shifted bits when chip select is released and accepts the
first sixteen payload bits of either command.

These behaviors follow [chip-level serial observations](https://forums.nesdev.org/viewtopic.php?t=21513).
They do not qualify GBA board propagation, the exact physical parameter sample
edge, abort/restoration behavior or clocks beyond the complete word. The
logical wire model uses the existing rising-edge payload sample; it adds no
speculative delay. Raw save bytes and the calendar remain independent.

Date and time use BCD with a two-digit year, leap-year rollover and a separately
stored weekday. In 12-hour mode, midnight and noon use hour zero; bit seven
distinguishes PM. In 24-hour mode, the hour supplies that read flag and writes
ignore it. Force-reset commands reset the calendar, control and interrupt parameters.
Console reset preserves the battery clock and clears the GPIO transaction.
The [RTC register description](https://problemkaputt.de/gbatek-gba-cart-real-time-clock-rtc.htm)
and [chip data sheet](https://www.rockby.com.au/DSheets/10110.pdf) describe the
registers and serial timing.

## Clock input

The core advances the calendar from emulated clocks. It never reads host time.
A new battery starts at `2000-01-01T00:00:00` in 24-hour mode with a healthy
power flag and zero interrupt parameters. Physical power-on calibration and
voltage sampling remain unqualified. Supply a valid seed from 2000 through
2099 with, for example:

```sh
cupidgba game.gba --rtc-time 2026-10-13T12:34:56
```

The seed overrides a loaded calendar for that launch. It does not overwrite
control or power flags. Weekday for a supplied seed is computed from its date;
the cartridge can subsequently write an independent weekday.

`--rtc-offline pause` is the default: the saved clock resumes from its previous
value, including its fractional second. For elapsed time while the frontend is
closed, use `--rtc-offline wall`. A fresh clock then starts from the host's
local date and time unless an explicit seed was supplied. Later launches add
positive elapsed host seconds to the persisted clock, preserving any time
offset written by the cartridge. Backward host-clock changes do not rewind it
or lower the saved anchor. A state last saved in pause mode has no host anchor,
so changing to wall mode does not infer elapsed time for that interval.

During execution both modes advance from emulated cycles. Pause mode supports
repeatable runs with a fixed seed and the same initial battery state. Wall mode
adds an explicit host input at startup and is not repeatable without recording
that input. Headless runs do not load or write frontend battery files.

The RTC has its own oscillator and continues during live console STOP. The
core accepts an explicit interval through `cupid_gba_advance_stopped_time`;
one supplied clock is 1/16777216 second of battery time. This leaves the CPU,
timers, video, audio, DMA and serial clocks and their deadlines frozen. The
call returns the clocks consumed, ending at the first eligible Game Pak
release edge even if IME is clear or CPU interrupts are masked. A disabled wake source
can retain a pending request while STOP continues. The request enters the
ordinary IF/available/IRQ pipeline when the system clock resumes.

The SDL2 frontend supplies one frame-length interval through
`cupid_gba_run_interval`. It consumes the stopped part on the battery clock and
the remainder on the system clock after wake. Completed CPU steps can cross an
interval endpoint; the next call charges that overshoot before running more
clocks. The headless runner also supplies deterministic STOP time and counts
it against its existing elapsed-clock budget. It reports `stopped_cycles`
separately; STOP creates no video frames or audio samples. Single CPU steps and
`cupid_gba_run_frame` still return while stopped and do not infer elapsed time.

## Battery files

The SDL2 frontend keeps a separate `.rtc` file beside the cartridge, using the
same base name. The raw `.sav` format and bytes are independent. Battery state
is flushed every 300 frames and on normal exit. It includes the calendar,
fractional second, control and power-detector state, cartridge fingerprint,
interrupt parameters, output latch and optional host-time anchor. It excludes GPIO and
in-progress transactions.

The 44-byte battery formats store integers in little-endian order. They have an
eight-byte cartridge fingerprint, seven binary calendar fields, a control
byte, a four-byte fraction in GBA clocks, one flag byte, three version-dependent
bytes, an eight-byte host anchor, and a four-byte checksum. The fingerprint
is FNV-1a-64 of the complete cartridge image; the checksum is FNV-1a-32 of the
preceding 40 bytes. These checks detect mismatches and accidental corruption.
Flag-byte bit 2 retains the low interrupt output. `CPDRTC01` reserves bytes
29 through 31 as zero and requires control bit 3 for a low output. `CPDRTC02`
stores the two interrupt-parameter bytes at offsets 29 and 30, reserves byte
31 as zero, and permits a low output with any interrupt mode enabled.
`CPDRTC03` keeps that layout and uses flag-byte bit 3 for the retained minute
carry signal. That flag requires a valid second zero and a fraction inside
the nominal 10 ms window; a minute mode enabled during that signal must have
low output. All three versions load. Versions 1 and 2 supply no retained carry
signal, and version 1 supplies zero parameters. Saves use version 3 while the
carry signal is active, version 2 when parameters or an alarm/frequency latch
require it, and version 1 otherwise.
Loading a battery or
advancing its offline clock schedules future transitions without replaying
console IRQs from the interval when the frontend was closed.

Invalid size, calendar, reserved fields, checksum or cartridge identity stops
startup before SDL initialization. Failed writes preserve the previous file.
If recovery is needed, preserve the rejected file before starting with a fresh
`.rtc` file and an explicit seed. `--rtc off` leaves an existing RTC file alone.

## Minute interrupts

Control bit 3 enables the minute output. With bit 1 clear, the chip output goes
low at the next minute carry and stays low until a control write or reset
acknowledges it. With bit 1 set, the output goes low at the minute carry and
returns high 30 seconds later. Enabling a mode waits for the next carry unless
the previous carry signal is still active.
Reading control or aborting an incomplete write does not acknowledge the latch.

The actual minute carry retains a processing signal for a nominal 10 ms.
Rewriting an enabled minute mode inside that interval reasserts the low output.
Disabling the mode releases it; enabling again inside the same interval makes
it low again. A write after that interval releases either minute mode until
the next carry, even during the steady mode's 30-second low phase. The window
starts at the calendar carry, not at the most recent control write. At the
core's clock rate, clock 167772 is inside the interval and clock 167773 is
outside it. The exact physical duration remains unmeasured.

A separate signal flag distinguishes a real carry from a new seed, reset or
time write with second zero. Clock advancement expires it using the original
fraction; console reset and pause-mode persistence retain the remaining window.

The cartridge's inverter passes the chip's rising release edge to the falling
Game Pak IRQ input. That edge requests IF bit 13 through the ordinary interrupt
pipeline. The first low transition does not request it. In steady mode, the
request occurs at second 30; in held mode, it occurs when software releases the
output. Disabling the mode or acknowledging it after the retained interval
releases a low output. GPIO read-control and pin direction do not disable this separate
interrupt connection.

An owned scheduler event dispatches each transition at its original deadline,
including during CPU stalls and late dispatch. Calendar writes reschedule from
the written time and reset divider fraction. Console reset retains the calendar,
fraction and output latch, while RTC disable or cartridge replacement cancels
the event. IRQ acknowledgement does not create another chip edge.

This model follows the minute-output waveforms in the chip data sheet and the
inverter connection in the GBA RTC register description linked above. Its board
edge is an inference from those descriptions. Direct cartridge captures are
still required to qualify that edge, the exact carry width and any transient
pulses while a status write acknowledges and reasserts output.

## Alarm and frequency interrupts

Control bit 3 selects minute output regardless of bits 1 and 5. When bit 3 is
clear, bit 1 selects frequency output; when both are clear, bit 5 selects alarm
output. With all three clear, the output is released. These modes share the
separate cartridge interrupt connection and scheduler described above.

In alarm mode, the low parameter byte holds the BCD hour and AM/PM flag; the
high byte holds the BCD minute. The format must match control bit 6. The AM/PM
flag must agree with the hour even in 24-hour mode. Reserved hour/minute bits
are ignored. The output latches low when the hour and minute match and stays
low until alarm mode is disabled. Rewriting an enabled alarm does not acknowledge
it. Disabling and enabling it during the matching minute asserts the low output
again. Invalid BCD or inconsistent 24-hour AM/PM parameters produce no match in
the model; invalid-parameter behavior still needs hardware qualification.

The scheduler splits alarm waits longer than its signed 32-bit horizon into
checkpoints. A checkpoint advances the calendar and schedules the remaining
wait; it does not create an output edge. Offline advancement retains an alarm
that became due during the interval, even if the final time no longer matches.

In frequency mode, parameter bits 0 through 15 select clocks from 32768 Hz
through 1 Hz. The chip output is the logical AND of the selected clocks. A zero
mask leaves it released. For example, `0x000A` selects 16384 Hz and 4096 Hz and
releases the output only while both clocks are high. The fraction of the current
second supplies the divider phase; the scheduler dispatches each output change
without polling every oscillator tick. Parameter/control writes preserve that
phase, and calendar/time writes reset it with the fractional second. The exact
physical phase after writes, zero-mask output and serial writes during active
outputs require cartridge captures.

## Qualification boundary

Four `rom.rtc-calendar-*` groups execute complete owned ARM/Thumb date/time
drivers through halfword and word GPIO accesses in all three ROM windows.
Their 3,072 complete records cover all one hundred years, February and other
month ends, every hour in both formats, independent weekdays and the full
minute/second range. Each record verifies the seven-byte date after writing,
the three-byte time after a separate write, the complete date again to retain
the untouched fields, and control readback. All 55,296 received serial bytes
must match independently constructed expected records. `CUPID_TEST_BIOS`
repeats the corpus with an external image, for 6,144 records and 110,592 bytes.
Another 24 guest trials poll through a complete RTC second and require the
2099-to-2000 year carry, weekday wrap and all seven carried date bytes.
Together the matrix and carry trials check 111,192 returned bytes. The carry
checks couple serial encoding to actual calendar advancement.

Missing-device controls must finish every record and fail each nonzero expected
byte. Separate controls change one expected byte per record and require exactly
256 failures while retaining the complete actual serial output. These checks
use explicit guest entry, fixed execution guards and owned inputs. They qualify
complete normal frames with interrupt outputs disabled. They do not qualify
active-output transactions or physical serial propagation.

The selected serial model keeps date/time reception active after a complete
payload while chip select remains high. Each subsequent complete payload
replaces the calendar or time. An incomplete following payload does not update
the calendar, whose clock continues to run. Deselecting discards that payload. These
updates retain the existing final-rising-edge commit and divider reset, with
no added delay. CupidGBA combines repeating serial framing with a complete-frame
write buffer. This is the selected logical model; behavior beyond the ordinary
frame boundary has no independent hardware confirmation.

The native `gba.rtc-calendar-framing` group checks 528 pin-driven scenarios:
both payload lengths and hour formats, every incomplete length, multiple full
frames, the final sample edge, repeated levels, deselection/restart and clock
epochs across the 32-bit boundary. A one-second pause during reception must
advance the battery clock before the next complete frame replaces its time.
The preceding core stops reception after its first payload and fails
the complete second-frame control. The checks qualify the selected framing
model; partial physical-register effects and electrical timing remain unmeasured.

Native tests cover calendar correction and rollover, wire commands, complete
responses, directions, ARM/Thumb instruction accesses, ROM preservation,
CPU/DMA word endpoints, burst addressing, reset and battery persistence.
An unchanged local status/state corpus checks 17 printed results plus its
presence/write guard. It exposed six failures in write completion, repeated
transfers, incomplete continuation, read corruption and invalid-command
recovery; the implemented state machine passes those original comparisons.
Five native groups retain rising-edge visibility, repeating frames, partial
status shifts, invalid-prefix recovery and all previous-bit suppression
positions with three data-transition patterns. The original source, inputs
and diagnostic transport stay private.
GPIO opcode prefetch now retains each port value sampled at its halfword
strobe. An owned scheduler event observes the input before a later CPU read;
buffered and in-flight ARM/Thumb fetches consume those saved values. A matching
entry survives an instruction-width change, including a word whose low halfword
has already completed. RAM data accesses leave sampling active, while Game Pak
data accesses, unrelated branch targets, console reset and cartridge replacement
discard it. Native tests cover every FIFO position, both width changes, partially
buffered GPIO words, the three ROM windows, both sequential waits, bulk versus
single-clock dispatch, clock wrap and DMA cancellation. Sampling uses the same
eight-halfword FIFO deadlines as CPU fetches; these tests are not physical
captures of the remaining WAITCNT and full-buffer timing edges.
An authorized gameplay replay has executed the cartridge's startup RTC reads
and reached the normal new-game flow. These results do not establish complete
peripheral accuracy.

Minute regressions cover held and steady outputs, divider fractions, calendar
carry, complete and aborted ARM/Thumb control writes, IF/IME/HALT sequencing,
reset, battery/offline state and event ownership. Bulk and single-clock dispatch
retain the same transitions and interrupt visibility while timer and DMA requests
run across clock wrap. Forced-IRQ commands request the Game Pak interrupt at the
original command-completion deadline, including late GPIO dispatch. Their physical
pulse length and the board inverter's response to that pulse remain unqualified.
Alarm/frequency regressions cover all 16 divider selections, mixed-clock
waveforms over a complete divider cycle, every hour in both formats, mode
priority, matching-minute reenable, held output, daily/calendar carries,
long-wait checkpoints and large offline intervals. ARM/Thumb parameter and
control stores check IRQ visibility and acknowledgement. Repeated frequency
edges retain timer/DMA chronology through late dispatch and clock wrap. The
battery formats, console reset, disable/replacement and frontend offline
resume preserve parameters and output state without replaying past requests.
These are documented-model regressions; the local original corpus does not
exercise alarm or frequency timing. Direct cartridge timing remains unqualified.
STOP regressions cover the independent fractional clock, all divider selections,
minute/alarm output latches, wake filtering and IRQ visibility, ARM/Thumb SWI
return on both BIOS paths, timer/video/audio/serial freezing, clock wrap, reset,
live versus offline persistence and chunked paced intervals. A generated
cartridge exercises GPIO configuration, STOP wake and a frozen timer through
the headless runner. Physical output phase, the cartridge inverter and the
system oscillator's stabilization delay remain unqualified.
Minute rearming tests cover both sides of the nominal 10 ms boundary, both
minute modes, disable/enable, fresh-zero versus real-carry state, mode priority,
power flags and bulk/chunked/offline clock input. ARM/Thumb GPIO completion
strobes retain the original deadline through late dispatch and clock wrap;
live STOP and console reset preserve the window. Version 3 roundtrips it with
output enabled or disabled, rejects impossible windows and preserves legacy
semantics without replaying powered-off requests. Time writes during an active
output still need further qualification.

The power detector exposes an explicit sample input: a failure latches the
flag and stops sampling; reading control rearms it. Voltage thresholds and
physical sampling phases are not scheduled. Mid-stream WAITCNT changes,
full-buffer restart and page-boundary timing still need hardware qualification.
Partial writes, electrical contention, floating inputs and invalid wire
timing also need further work. Issue #28 remains open for these gaps.
