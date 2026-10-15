# Rebuilding the replacement BIOS

The embedded replacement BIOS has assembly source in
`src/gba/bios/hle-bios.s`. Rebuild its bytes with Clang and LLD:

```sh
mkdir -p .local-research
clang --target=arm-none-eabi -march=armv4t -fuse-ld=lld -nostdlib \
  '-Wl,-Ttext=0,--image-base=0,--oformat=binary,-e,0' \
  src/gba/bios/hle-bios.s -o .local-research/hle-rebuilt.bin
python3 tests/bios/verify_hle.py .local-research/hle-rebuilt.bin
python3 tests/bios/generate_affine_table.py --check
python3 tests/bios/generate_music_table.py --check
python3 tests/bios/generate_checksum_identity.py .local-research/hle-rebuilt.bin --check
```

The verifier compares all 16,384 explicit bytes with `src/gba/hle-bios.c`.
The included `src/gba/bios/data-services.s` contains the timed data utilities and
bias ramp. The verifier checks the internal stall entry, the ARM/Thumb service
targets, and the utility source guard's observable address at `0x0BA4`.
`src/gba/bios/affine-services.s` contains the timed affine routines and includes
`affine-sine.inc`. Its 256 signed 1.14 sine values are reproduced with decimal
arithmetic by `tests/bios/generate_affine_table.py`; `--write` regenerates the
include and `--check` verifies it. No external firmware data is needed.
`register-reset.s` contains the timed register/memory reset. `copy-fast.s`
contains its direct Thumb-to-ARM bridge and the shared block-transfer code.
`soft-reset.s` contains restart selection, bank setup and the timed work-area
clear, followed by an ARM pipeline refill at the selected entry address.
`hard-reset.s` shares that bank/work initialization and performs warm startup,
register/memory resets and the bias ramp through the CPU. Its Thumb startup
pointer carries the function's instruction-state bit. GNU and LLVM builds
produce identical bytes.
`power-control.s` contains the shared ARM HALTCNT byte strobe and return for
HALT, STOP and CustomHalt. Unused space after HALT's branch preserves the
fixed internal entry offsets without adding executed delay instructions.
`music-frequency.s` contains timed Thumb interpolation and its ARM multiply-high
bridge. `music-pitch.inc` provides 180 octave/semitone indices and 12 Q31
semitone factors reproduced with decimal arithmetic. Its generator supports
`--write` and `--check`. The reset pointer subtraction has an explicit Thumb
encoding so GNU and LLVM emit identical bytes for the same operation.
`sound-mixer.s`, `sound-driver.s` and `sound-init.s` occupy reserved space
before the pitch data. They contain the software mixer, work-area controls,
PCM clearing, FIFO DMA cadence, playback-rate configuration and initialization.
The integer ratio helper enters ARM with a direct instruction-state change.
Mixing and reverb use ordinary signed integer instructions. Reserved music
helper pointers and the internal jump table remain callable placeholders;
their undocumented operations are not implemented.
`checksum.s` reads all 4,096 image words through timed ARM loads and adds them.
The reserved final word at `0x3FFC` comes from `checksum-identity.inc`. Its
generator subtracts the sum of the preceding words from the replacement
identity `0xBAAE187F`, modulo 32 bits. This makes the embedded image's actual
sum compatible with its checksum service; it does not reproduce external
firmware bytes. All padding and the final word are explicit in the 16 KiB image.
After changing assembly, assemble once, run the identity generator with
`--write`, reassemble and update the embedded bytes. `--check` verifies both
the include and the completed image. Linux Clang CI runs the source, identity
and mathematical table checks. External BIOS dumps are separate inputs.
