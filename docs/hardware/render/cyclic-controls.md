# Repeated OBJ control observations

The pinned original display cartridge continuously enables and disables OBJ
rendering through HALT and VCOUNT interrupts. Its evaluation routine and idle
result branch are unreachable during that loop. A zero result register therefore
does not qualify this program. Two source filenames contain identical cartridge
bytes; they are one input, not two independent tests.

`rom.display-control-cycles` requires 600 completed frames and at least 300
complete enable/disable sequences. At actual issue of each original Thumb
halfword store, it checks both the control value and its I/O base register.
The issued addresses, CPU mode, opcodes, complete cycle count and any final
partial prefix must agree with the unchanged input contract. Checking continues
after the minimum; missing, reordered or late incorrect stores fail.

Fetch the verified cartridge and its license with:

```sh
python3 tests/roms/check_cyclic_display.py --fetch --directory .local-research/test-roms
```

Configure `CUPID_DISPLAY_PROBE_DIR` to that directory. An authorized
`CUPID_TEST_BIOS` adds an external-firmware run. Both use the same frame, step
and process limits. CI runs the built-in-firmware check. To save an image for
inspection, use the checker directly with `--runner PATH --directory PATH
--screenshot OUTPUT.ppm` and optionally `--bios PATH`.

This checks control-loop execution. It does not compare the screenshot with a
physical capture or qualify the final OBJ pixels. The disputed behavior is
whether the final texture request produces visible pixels before the engine
stops, and how live OBJ enable changes interact with pending output. The
[texture-tail bus check](object-tail.md) qualifies a CPU-ready boundary separately.
Shortening VRAM ownership to change the picture would invalidate that evidence.
Pixel output remains an open accuracy requirement.
