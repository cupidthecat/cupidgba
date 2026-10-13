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

CPU and DMA word data accesses use two halfword strobes. The first port is
sampled or written before the second strobe, with scheduler events running
between them. DMA uses the cartridge burst counter to select the physical
port. Byte stores currently leave the port unchanged. That byte policy and
the alias decoder still need direct hardware qualification.

The RTC accepts control, date and time transactions over clock, serial-data
and chip-select pins. Command bits are sampled on rising clock edges; payload
writes and outgoing data advance on falling edges. Reads capture a response
when the command completes. Chip-select release aborts a transaction. Date
writes commit after a complete frame; partial-write behavior needs a hardware
capture. Extra clocks after a response do not restart it.

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
and optional host-time anchor. It excludes GPIO and in-progress transactions.

The 44-byte `CPDRTC01` format stores integers in little-endian order. It has an
eight-byte cartridge fingerprint, seven binary calendar fields, a control
byte, a four-byte fraction in GBA clocks, one flag byte and three reserved
zero bytes, an eight-byte host anchor, and a four-byte checksum. The fingerprint
is FNV-1a-64 of the complete cartridge image; the checksum is FNV-1a-32 of the
preceding 40 bytes. These checks detect mismatches and accidental corruption.

Invalid size, calendar, reserved fields, checksum or cartridge identity stops
startup before SDL initialization. Failed writes preserve the previous file.
If recovery is needed, preserve the rejected file before starting with a fresh
`.rtc` file and an explicit seed. `--rtc off` leaves an existing RTC file alone.

## Qualification boundary

Native tests cover calendar correction and rollover, wire commands, complete
responses, directions, ARM/Thumb instruction accesses, ROM preservation,
CPU/DMA word endpoints, burst addressing, reset and battery persistence.
An authorized gameplay replay has executed the cartridge's startup RTC reads
and reached the normal new-game flow. These results do not establish complete
peripheral accuracy.

Forced-IRQ commands currently request the Game Pak interrupt when the command
completes. The physical pulse length and the board inverter's edge remain
unqualified. Periodic RTC IRQ generation is not implemented. Control bits for
those modes retain their values without scheduling periodic interrupts.

The power detector exposes an explicit sample input: a failure latches the
flag and stops sampling; reading control rearms it. Voltage thresholds and
physical sampling phases are not scheduled. Prefetched GPIO opcode sampling,
partial writes, electrical contention, floating inputs and invalid wire
timing also need further work. Issue #28 remains open for these gaps.
