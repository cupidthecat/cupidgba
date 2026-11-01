# Timer and DMA suite checks

[Testing overview](../testing.md). Run commands from the repository root.

## Original timer and DMA cartridges

The three hardware timer ROMs check reload timing, start/stop timing, and
overflow before the enable reload. The observer reads their visible console
text from VRAM and requires every expected PASS line and the completion message.
It rejects a FAIL line or an incomplete result. It does not modify the ROM,
counter values, or expected results. These checks use the replacement BIOS;
`check_console.py --bios /path/to/gba-bios.bin` can run the same checks with
an external dump.
The timer and DMA observers allow 400 frames with an external BIOS to include
its startup animation, and 20 frames with the built-in path. The VRAM-mirror
case uses 120 built-in frames for its two CRC loops and the same 400 external
frames. Both paths require all ten original mirror/CRC results. Subprocess and
CTest deadlines are unchanged.

Four hardware DMA ROMs require ten assertions: immediate sampling at 20 clocks,
two 88-clock burst-restart cases, four latch cases, and three cartridge-crossover
results including its 41-clock duration. They use the same console observer and
reject incomplete or failed results. Setting `CUPID_TEST_BIOS` also registers
these four external-BIOS checks in CTest.

## Count-up and IRQ suite

The optional locally built count-up/IRQ suite has a separate observer:

```sh
python3 tests/roms/check_timer_suite.py --runner build/cupidgba-headless \
  --rom /path/to/timer-suite.gba --bios /path/to/gba-bios.bin
```

The observer requires the pinned input digest, the terminal instruction,
R12=0, exactly 936/936 count-up results and 90/90 IRQ results, and no logged
failures. Set `CUPID_TIMER_SUITE_ROM` when configuring CMake to include the
built-in path in CTest. Set `CUPID_TEST_BIOS` to add the external-BIOS suite
and native wait comparisons. Neither input is committed or required by hosted
CI. The suite checks divided clocks, interrupt handlers, BIOS interrupt waits,
and counter observations inside ARM loops. Local source revisions, compiler
settings and the build recipe stay with the ignored research checkout.

Both BIOS paths pass 936/936 count-up results and 90/90 IRQ results on the same
input. Omit `--bios` from the command above to check the built-in path.
These results establish the tested wait and timer interactions and do not
establish complete BIOS parity.
See [BIOS execution](../../hardware/bios.md) for the wait-loop behavior and limits.

## Owned timer edges

The owned timer edge matrix needs no external input:

```sh
cmake --build build --target test_timer_edges --parallel 2
ctest --test-dir build -R '^gba[.]timer-edges-' --output-on-failure
```

Its sixteen channel/prescaler groups retain a 30-second deadline each. They
check all prescaler phases and access widths, stopped-FFFF enable behavior,
cascade observations, IRQ acknowledgement before/at/after overflow and actual
clock-boundary crossings. The expected states use overflow deadlines and
observed register sequences; the fixture never assigns a timer's counter.
See [timers](../../hardware/timers.md) for the selected timing and coverage limits.

## DMA transfer suite

The optional local DMA transfer suite has its own observer:

```sh
python3 tests/roms/check_dma_suite.py --runner build/cupidgba-headless \
  --rom /path/to/dma-suite.gba --bios /path/to/gba-bios.bin
```

It requires the pinned input digest, terminal instruction, R12=0, exactly
1244/1244 results and no logged failures. Set `CUPID_DMA_SUITE_ROM` to include
the built-in path in CTest and `CUPID_TEST_BIOS` to add the external path.
The private harness leaves the original tests and expected values unchanged.
Both paths pass. This suite checks transfer data; the four hardware ROMs check
the listed timings and latches. See [DMA and bus ownership](../../hardware/dma.md)
for the behavior and remaining limits.
