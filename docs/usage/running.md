# Running games

```sh
cupidgba game.gba [-b bios.bin] [-s scale] [-S savetype]
```

`-b` loads an external GBA BIOS dump containing exactly 16,384 bytes. An invalid
requested BIOS stops startup with an error; see [BIOS image loading](../hardware/bios/image-loading.md).
Without it, the emulator uses its built-in
replacement BIOS and HLE calls. `-s` selects integer window scaling from 1 to 10
(default 3). Supply a BIOS and ROM you are authorized to use.

The emulator detects save hardware from the ROM. `-S` overrides it with `sram`,
`sram512`, `flash512`, `flash1m`, `sst39vf512`, `eeprom`, `eeprom512`, or `none`.
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
