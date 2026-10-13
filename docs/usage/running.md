# Running games

```sh
cupidgba game.gba [-b bios.bin] [-s scale] [-S savetype]
```

`-b` loads an external GBA BIOS dump. Without it, the emulator uses its built-in
replacement BIOS and HLE calls. `-s` selects integer window scaling from 1 to 10
(default 3). Supply a BIOS and ROM you are authorized to use.

The emulator detects save hardware from the ROM. `-S` overrides it with `sram`,
`sram512`, `flash512`, `flash1m`, `eeprom`, `eeprom512`, or `none`. The SDL2 frontend
stores battery saves next to the ROM using the same base name and `.sav`
extension. It flushes them periodically and on exit. Invalid or incompatible
save files stop startup with an error. Failed writes keep the previous file;
see [save files](../hardware/cartridge/save-files.md) for capacities, overrides
and recovery behavior.

Keyboard controls are Z for A, X for B, Enter for Start, Backspace for Select,
the arrow keys for the D-pad, A for L, S for R, and Escape to quit. SDL2
controllers use A/B, Back, Start, the D-pad, and the shoulder buttons. The left
stick also controls the D-pad.
