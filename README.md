# CupidGBA

CupidGBA is a Game Boy Advance emulator written in C with SDL2 for video, audio, and input.

## Current scope

- Emulates **Game Boy Advance (`.gba`)** ROMs
- Uses optional real BIOS (`-b`) or built-in HLE BIOS fallback
- Supports battery-backed save types (auto-detect + manual override)
- Renders at native GBA resolution `240x160` with integer window scaling

## Requirements

- Linux (or another platform with SDL2)
- `gcc`
- `make`
- `pkg-config`
- `SDL2` development package

On Debian/Ubuntu:

```bash
sudo apt update
sudo apt install build-essential pkg-config libsdl2-dev
```

## Build

From the project root:

```bash
make clean && make
```

This produces the executable:

- `./cupidgba`

## Usage

```bash
./cupidgba rom.gba [-b bios.bin] [-s scale] [-S savetype]
```

Options:

- `-b <path>`: Load real GBA BIOS image (optional)
- `-s <1..10>`: Window scale factor (default: 3)
- `-S <type>`: Force save type instead of auto-detect

Save types for `-S`:

- `sram`
- `sram512`
- `flash512`
- `flash1m`
- `eeprom`
- `eeprom512`
- `none`

Example:

```bash
./cupidgba /path/to/game.gba -s 4 -b bios/gba_bios.bin
```

## Controls

Keyboard:

- `Z` = A
- `X` = B
- `Backspace` = Select
- `Enter` = Start
- `Arrow Keys` = D-Pad
- `A` = L
- `S` = R
- `Esc` = Quit

Gamepad (SDL controller mapping):

- `A/B` = A/B
- `Back` = Select
- `Start` = Start
- `D-Pad` = D-Pad
- `LB/RB` = L/R
- Left analog stick also maps to D-Pad (with deadzone)

## Save files

- Save data is stored next to the ROM, using the same base name with `.sav`
- Example: `Pokemon.gba` → `Pokemon.sav`
- Saves are periodically flushed during play and once again on exit

## Notes

- This project currently targets **GBA ROMs only**.
- Supplying non-GBA ROM formats may fail or crash.

## Project layout

- `src/` — emulator source
  - `src/arm/` — ARM7TDMI CPU core
  - `src/gba/` — GBA subsystems (memory, video, audio, DMA, timers, BIOS, savedata)
  - `src/gba/render/` — software renderer modes
  - `src/core/` — timing/event scheduler
  - `src/platform/` — SDL frontend (`main`, audio, input)
- `include/cupid/` — public/internal headers
- `bios/` — optional location for BIOS image(s)

## Troubleshooting

- Build fails with missing SDL2: ensure `libsdl2-dev` and `pkg-config` are installed.
- No audio: emulator continues running if audio init fails.
- Crashes with `.gb` / `.gbc`: use `.gba` ROMs only.
