# Serial and local-session checks

[Testing overview](../testing.md). Run commands from the repository root.

## Serial and HALT cartridges

The serial and HALT cartridge checks retain their original 24 encoded
comparisons, result-display entry and completed terminal. Run their owned
protocol and execution controls with `ctest -R '^runner[.]immediate-comparison-(protocol|execution)$'`.
The execution control demonstrates why a wrong zero measurement or a skipped
comparison can leave a passing-looking result register. Both must be rejected.

Linux and Windows CI fetch the checksum-pinned serial inputs and their license
notice, then run all nine built-in-BIOS serial cases. To fetch them locally:

```sh
python3 tests/roms/check_serial_probe.py --fetch --directory .local-research/serial
cmake -S . -B build -DCUPID_SERIAL_PROBE_DIR="$PWD/.local-research/serial"
ctest --test-dir build -R '^rom[.]serial-probe-' --output-on-failure
```

Supplying `CUPID_TEST_BIOS` repeats the nine cases with that authorized BIOS.
The four HALT cartridges can also be run individually with
`check_serial_probe.py --directory .local-research/serial --runner build/cupidgba-headless --bios /path/to/gba_bios.bin --test halt_pc`.
The complete `CUPID_HALT_PROBE_DIR` profile additionally needs the separate
IRQ corpus. BIOS files are never downloaded or distributed.

## Local sessions and display

Local-session checks are `gba.cable-*`, `gba.video-cable-*` and `platform.local-*`. They cover
complete paired guest bytes and words, multiplayer receive slots, bus/event
coordination, cancellation, STOP, SDL input/video/audio and independent battery
files. Guest matrix cases have separate CTest entries; each retains its clock
guard and every payload comparison. See [local sessions](../../hardware/serial/local-session.md)
for the host API and scope. With `CUPID_TEST_BIOS`, the owned word and UART
guest cases also run with the external firmware. Reconstruct `cable-driver.s`
at `0x03001000` with the same ARMv4T binary-link flags as the UART driver and
verify all 512 bytes with `tests/gba/serial/generate_cable_driver.py`.

The two display/session groups compare 768 complete display snapshots and
29,491,200 pixels at sixteen requested endpoints. HALTed and active CPUs,
pending EWRAM fetches, two- and four-console sessions, a frozen STOP console,
mid-line memory writes and clock wrap must match independent clock advances.
Each group has a ten-second deadline and runs with native and portable context
switching. Both reject the preceding session clock-completion path.

## Joy Bus

The eight `gba.joybus-*` groups check complete command/reply data, 1,024 payload
patterns, 10,740 rejected frames, decoded register accesses, IRQ/HALT, DMA/timer
chronology and lifecycle. The owned ARM/Thumb guest completes 256 receive/send
rounds and two failing-input controls per BIOS path; `CUPID_TEST_BIOS` adds the
external-image run. All retain ten-second deadlines. Hosted Clang also rebuilds
the owned assembly fixture. See [JOY commands](../../hardware/serial/joybus.md)
for the peer synchronization contract and unmeasured electrical behavior.

## Original serial suite

The optional local serial suite retains four counted normal timing results,
90 register observations and four original multiplayer timeouts:

```sh
python3 tests/roms/check_serial_suite.py --runner build/cupidgba-headless \
  --rom /path/to/serial-suite.gba --bios /path/to/gba-bios.bin
```

Set `CUPID_SERIAL_SUITE_ROM` to register the built-in path and `CUPID_TEST_BIOS`
to add the external path. The observer requires its pinned digest, terminal
R12=0, complete counts, every case flag and zero failure logs. The multiplayer
timeouts remain skipped and are never counted as working transfers. The private
wrapper initializes normal data registers between the timing and read suites,
so the read suite starts from its original reset-state precondition; it retains
all timed code and expectations. Additional single-write startup timing failures
are described in [serial behavior](../../hardware/serial/normal-and-gpio.md).

## Standalone probe configuration

Optional standalone serial and HALT probes use their original cartridge bytes
and comparisons. Set `CUPID_SERIAL_PROBE_DIR` for the nine serial IRQ, data and
start-bit probes. Set `CUPID_HALT_PROBE_DIR` and `CUPID_TEST_BIOS` for four HALT
timer/return-address probes that observe the original firmware's internal
addresses. The observer checks each input digest and passing terminal result.
It retains the 360-frame, 150-million-step and 90-second observation budgets.
Native IRQ tests cover the acceptance and wake samples in ARM/Thumb mode,
their saved return addresses and clock wrap.

## Native serial coverage

Serial regressions exercise both widths and clock rates, all 256 transmitted
8-bit patterns, driven input data, external clock edges, two connected core
instances, GPIO directions and falling-input IRQs, ARM/Thumb bus writes,
untouched byte fields, late dispatch, clock wrap, reset and power-state behavior.
