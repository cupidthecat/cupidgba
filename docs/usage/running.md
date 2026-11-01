# Running games

```sh
cupidgba game.gba [-b bios.bin] [-s scale] [-S savetype] [--frames count]
```

`-b` loads an external GBA BIOS dump containing exactly 16,384 bytes. An invalid
requested BIOS stops startup with an error; see [BIOS image loading](../hardware/bios/image-loading.md).
Without it, the emulator uses its built-in
replacement BIOS and HLE calls. `-s` selects integer window scaling from 1 to 10
(default 3). Supply a BIOS and ROM you are authorized to use.

`--frames N` exits after N paced frame-length intervals through the normal
shutdown path, including its save and RTC flushes. N must be a positive integer.
STOP still consumes these intervals even when it produces no new video frame.
Closing the window or pressing Escape can end the run sooner. Omit the option
for ordinary play. It also applies to [local sessions](../hardware/serial/local-session.md).

The emulator detects save hardware from the ROM. `-S` overrides it with `sram`,
`sram512`, `flash512`, `flash1m`, `sst39vf512`, `eeprom`, `eeprom512`, or `none`.
`auto` retains ROM detection. Invalid names stop startup before any cartridge
runs or battery file changes. Single-console and local sessions use the same
selection rules.
`sst39vf512` selects a fixed 64-KiB Flash device with its documented identity,
command set and nominal programming intervals; see [save programming](../hardware/cartridge/programming.md).
The SDL2 frontend
stores battery saves next to the ROM using the same base name and `.sav`
extension. It flushes them periodically and on exit. Invalid or incompatible
save files stop startup with an error. Failed writes keep the previous file;
see [save files](../hardware/cartridge/save-files.md) for capacities, overrides
and recovery behavior.

RTC cartridges use a separate `.rtc` battery file. `--rtc auto|on|off` selects
the hardware; `--rtc-time YYYY-MM-DDTHH:MM:SS` supplies a calendar seed.
`--rtc-offline pause|wall` chooses whether a saved clock includes elapsed
host time while the frontend is closed. The default is pause. See
[cartridge RTC](../hardware/cartridge/rtc.md) for persistence and current limits.

`--solar auto|on|off` and `--rumble auto|on|off` select cartridge peripherals.
`--sunlight 0..255` sets relative illumination (default 128); larger values
represent more light. F5 lowers sunlight and F6 raises it while running.
A connected SDL2 controller provides rumble when supported. See
[cartridge peripherals](../hardware/cartridge/peripherals.md) for detection
and current hardware limits.

`--gyro auto|on|off` selects the serial gyroscope. `--gyro-sample 0..4095`
sets its raw converter input, defaulting to nominal neutral 1728. F7 lowers
input, F8 raises it and F9 restores that nominal value. This is manual input;
see [gyroscope behavior and limits](../hardware/cartridge/gyroscope.md).

`--tilt auto|on|off` selects the X/Y sensor. `--tilt-x 0..4095` and
`--tilt-y 0..4095` set raw converter inputs, each defaulting to 928. F1/F2
lower/raise X, F3/F4 lower/raise Y and F10 restores both nominal values.
See [tilt snapshots and timing limits](../hardware/cartridge/tilt.md).

Keyboard controls are Z for A, X for B, Enter for Start, Backspace for Select,
the arrow keys for the D-pad, A for L, S for R, and Escape to quit. SDL2
controllers use A/B, Back, Start, the D-pad, and the shoulder buttons. The left
stick also controls the D-pad.

Keyboard, controller buttons and stick directions combine, so a button remains
held until all sources release it. Removing the selected controller selects
the next available one. Window focus loss releases keyboard holds. See
[input handling](../hardware/input.md) for device ownership and regression
coverage.

## Headless RTC replays

`cupidgba-headless` also accepts `--rtc auto|on|off` and `--rtc-time`.
Use `--load-rtc FILE` to read a cartridge-bound battery and `--dump-rtc FILE`
to write the resulting battery explicitly. Headless runs do not load adjacent
RTC files or advance a loaded clock from host wall time. A supplied date takes
precedence over the loaded calendar. These options require an enabled RTC;
use `--rtc on` when testing a cartridge without automatic RTC detection.
See [reproducible RTC runs](../development/testing/runtime.md#deterministic-rtc-input-and-snapshots)
for commands, validation and the distinction between batteries and save states.

## Headless backup selection

`cupidgba-headless --save-type TYPE` (also `-S TYPE`) accepts the same backup
names as the frontend. Select the device before `--load-save FILE` restores
its battery. Explicit capacities do not expand to accommodate a larger file;
incompatible inputs fail before execution or output creation. With no override,
the existing ROM detection and battery-size inference remain in effect.

The older `--flash-profile sst39vf512` option remains supported. It implies
64-KiB Flash and can accompany `--save-type auto`, `flash512`, or `sst39vf512`.
Combining it with another device is an error in either argument order.
`--dump-save FILE` exports committed battery bytes, not an in-progress program
operation or emulator save state. Headless runs still never load or save an
adjacent battery implicitly.
