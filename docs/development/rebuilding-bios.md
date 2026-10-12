# Rebuilding the replacement BIOS

The embedded replacement BIOS has assembly source in
`src/gba/bios/hle-bios.s`. Rebuild its bytes with Clang and LLD:

```sh
mkdir -p .local-research
clang --target=arm-none-eabi -march=armv4t -fuse-ld=lld -nostdlib \
  '-Wl,-Ttext=0,--image-base=0,--oformat=binary,-e,0' \
  src/gba/bios/hle-bios.s -o .local-research/hle-rebuilt.bin
python3 tests/bios/verify_hle.py .local-research/hle-rebuilt.bin
```

The verifier compares all 1,856 explicit bytes with `src/gba/hle-bios.c`.
It also checks the shared stall entries and the ARM/Thumb math/copy targets
against the assembly's dispatch offsets.
The remaining space in the 16 KiB C array is zero-initialized. Linux Clang CI
also runs this comparison. External BIOS dumps are separate inputs.
