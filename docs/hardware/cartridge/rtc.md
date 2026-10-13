# Cartridge GPIO and RTC

CupidGBA detects an RTC driver signature in the cartridge image. `--rtc on`
enables the device explicitly, `--rtc off` disables it, and `--rtc auto` uses
signature detection. The default is automatic. Detection does not patch CPU
execution or change cartridge bytes. Other GPIO devices are not implemented.

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

The RTC accepts control, date and time transactions over clock, serial-data
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
Date writes commit after a complete frame; their partial and overrun behavior
still needs qualification.

Date and time use BCD with a two-digit year, leap-year rollover and a separately
stored weekday. In 12-hour mode, midnight and noon use hour zero; bit seven
distinguishes PM. In 24-hour mode, the hour supplies that read flag and writes
ignore it. Force-reset commands reset the calendar and control registers.
Console reset preserves the battery clock and clears the GPIO transaction.
The [RTC register description](https://problemkaputt.de/gbatek-gba-cart-real-time-clock-rtc.htm)
and [chip data sheet](https://www.rockby.com.au/DSheets/10110.pdf) describe the
registers and serial timing.

## Clock input

The core advances the calendar from emulated clocks. It never reads host time.
A new battery starts at `2000-01-01T00:00:00` in 24-hour mode with a healthy
power flag. Supply a valid seed from 2000 through 2099 with, for example:

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

## Battery files

The SDL2 frontend keeps a separate `.rtc` file beside the cartridge, using the
same base name. The raw `.sav` format and bytes are independent. Battery state
is flushed every 300 frames and on normal exit. It includes the calendar,
fractional second, control and power-detector state, cartridge fingerprint,
minute-output latch and optional host-time anchor. It excludes GPIO and
in-progress transactions.

The 44-byte `CPDRTC01` format stores integers in little-endian order. It has an
eight-byte cartridge fingerprint, seven binary calendar fields, a control
byte, a four-byte fraction in GBA clocks, one flag byte and three reserved
zero bytes, an eight-byte host anchor, and a four-byte checksum. The fingerprint
is FNV-1a-64 of the complete cartridge image; the checksum is FNV-1a-32 of the
preceding 40 bytes. These checks detect mismatches and accidental corruption.
Flag-byte bit 2 retains the low minute output; it is valid only with control
bit 3 enabled. Files with that bit clear remain valid. Loading a battery or
advancing its offline clock schedules future transitions without replaying
console IRQs from the interval when execution was stopped.

Invalid size, calendar, reserved fields, checksum or cartridge identity stops
startup before SDL initialization. Failed writes preserve the previous file.
If recovery is needed, preserve the rejected file before starting with a fresh
`.rtc` file and an explicit seed. `--rtc off` leaves an existing RTC file alone.

## Minute interrupts

Control bit 3 enables the minute output. With bit 1 clear, the chip output goes
low at the next minute carry and stays low until a complete control write or
reset releases it. With bit 1 set, the output goes low at the minute carry and
returns high 30 seconds later. Enabling the mode waits for the next carry.
Reading control or aborting an incomplete write does not acknowledge the latch.

The cartridge's inverter passes the chip's rising release edge to the falling
Game Pak IRQ input. That edge requests IF bit 13 through the ordinary interrupt
pipeline. The first low transition does not request it. In steady mode, the
request occurs at second 30; in held mode, it occurs when software releases the
output. Changing or disabling the mode with a complete control write releases
a low output. GPIO read-control and pin direction do not disable this separate
interrupt connection.

An owned scheduler event dispatches each transition at its original deadline,
including during CPU stalls and late dispatch. Calendar writes reschedule from
the written time and reset divider fraction. Console reset retains the calendar,
fraction and output latch, while RTC disable or cartridge replacement cancels
the event. IRQ acknowledgement does not create another chip edge.

This model follows the minute-output waveforms in the chip data sheet and the
inverter connection in the GBA RTC register description linked above. Its board
edge is an inference from those descriptions. Direct cartridge captures are
still required to qualify that edge and writes during minute-carry processing.

## Qualification boundary

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
buffered and in-flight ARM/Thumb fetches consume those saved values. RAM data
accesses leave sampling active, while Game Pak data accesses, branch/width
changes, console reset and cartridge replacement discard it. Native tests
cover every FIFO position, the three ROM windows, both sequential waits,
bulk versus single-clock dispatch, clock wrap and DMA cancellation. The model
retains the existing prefetch counter timing; these tests are not physical
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
Control bit 1 without bit 3, alarm mode, and their write-only frequency/alarm
parameters are not implemented or qualified on the cartridge. Those flags retain
their values. Minute hold rearming within the chip's 10 ms carry interval, time
writes during an active output and STOP wake timing also need physical evidence.

The power detector exposes an explicit sample input: a failure latches the
flag and stops sampling; reading control rearms it. Voltage thresholds and
physical sampling phases are not scheduled. Mid-stream WAITCNT changes,
full-buffer restart and page-boundary timing still need hardware qualification.
Partial writes, electrical contention, floating inputs and invalid wire
timing also need further work. Issue #28 remains open for these gaps.
