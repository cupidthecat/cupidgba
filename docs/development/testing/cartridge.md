# Cartridge and save checks

[Testing overview](../testing.md). Run commands from the repository root.

## EEPROM and save programming

`CUPID_EEPROM_PROBE_ROM` adds a checksum-verified local EEPROM software corpus.
Its ready-polling and 1,023-block stress loop must reach the original terminal
and complete passing result pixels. `CUPID_TEST_BIOS` adds the same check after
firmware boot. The observer uses a fixed 1,200-frame, 150-million-step and
90-second process budget; CTest retains a 100-second deadline. This separately
qualified software input is not distributed and does not calibrate chip
timings. See [save programming](../../hardware/cartridge/programming.md).

The original nine `gba.save-programming-*` groups cover accepted operations, ready
deadlines, polling, bus strobes, timer/DMA/IRQ chronology, clock wrap, STOP,
reset, profile validation and complete save-file images through failures and
replacement. A 32-case reboot group retains every captured operation and its
original deadline through ARM/Thumb HardReset at four clock epochs, including
signed/unsigned wrap. Entry is observed after each step return inside a batch,
with the original 500,000-step cap per scene. Count zero cannot complete a reboot.
`-DCUPID_TEST_JOBS=2` runs its independent ARM and Thumb scenes in two isolated
processes. The worker checker requires every ordered case and both completed
worker summaries. The default uses the full serial group. CI selects two workers
for the unoptimized native build; the whole group retains its ten-second limit.
An owned ARM driver also polls through the default byte-program
interval from IWRAM and ROM. Synthetic operation durations separate scheduler correctness
from chip calibration. Each group retains a ten-second deadline; see
[save programming](../../hardware/cartridge/programming.md).

`gba.save-programming-rtc-stop` adds 192 complete interactions between save
programming and an RTC wake during STOP. The five `gba.flash-profile-*` groups
cover explicit chip selection, nominal ready edges, complete images, actual
ARM/Thumb strobes and lifecycle; each has a ten-second deadline.
`rom.flash-profile` runs an owned command/poll driver and requires default-device
and wrong-ID controls to fail. An external BIOS adds the same probe at an
explicit cartridge entry. `platform.flash-profile` uses bounded local sessions
to check complete independent saves and both startup paths' capacity rejection.
These probes qualify the documented nominal model, without physical timing
calibration. All original source-ROM inputs, assertions and budgets remain.

The Flash DMA group checks 216 full scenes: all three profiles, every operation,
before/at/after reads, four clock epochs and bulk/partitioned dispatch. Each
scene checks complete storage at four bus phases, captured read data, guarded
destination bytes, timer/DMA IRQs and CPU release. Its ten-second deadline
qualifies integration with the existing bus model.

The six `rom.flash-driver-*` entries add complete ARM/Thumb program, sector,
bank and chip transactions for both default Flash capacities and the explicit
64-KiB profile. Each compares the entire final saved image through guest bus
reads, retains memory guards and requires a wrong-comparison control to fail.
An external BIOS adds the same six cases at an explicit cartridge entry.
Each process retains a 120-frame, 50-million-step and ten-second bound; each
CTest entry has thirty seconds for its positive and negative cases.

## GPIO byte transactions

The two `gba.gpio-byte-*` groups cover 1,536 complete port frames and 576
strobe/wait/timer/IRQ cases. The `rom.rtc-calendar-byte-*` groups retain the
complete calendar matrix, carry trials and negative controls through actual
ARM/Thumb byte instructions, both store-address lanes and both firmware paths.
The earlier halfword/word inputs and expectations remain byte-identical.
Native checks retain ten-second deadlines; guest groups retain thirty-second
deadlines and their original frame/step/process guards. See
[cartridge GPIO](../../hardware/cartridge/rtc.md) for behavior and limits.

`gba.rtc-active-transactions` checks 1,968 active/inactive calendar frame and
abort cases plus 3,120 parameter-word overrun/restart cases. Its ten-second
deadline covers complete output/deadline comparisons across clock wrap.
These checks cover the selected logical policy and leave physical serial
propagation and partial-register effects unqualified.

## Image mapping and peripherals

The `gba.cartridge-image-*` groups cover EEPROM address selection, complete
DMA3 serial frames, original image size, replacement ownership and injected
allocation/file failures. The five `gba.cartridge-ownership-*` groups check all
49 detected and 49 explicit-device transitions, marker boundaries, pending-event cancellation and
rebinding, busy/nonregular input, output-callback ordering and sensor ownership.
Failure injection compiles the production transaction with wrapped host
operations; it does not substitute an alternate loader. They retain
the ten-second deadlines; see [cartridge image mapping](../../hardware/cartridge/image-mapping.md).

Cartridge peripheral checks include `gba.solar-*`, `gba.cartridge-rumble`,
`gba.gyro`, `platform.cartridge-peripherals`, `rom.cartridge-gyro` and
`rom.cartridge-peripherals`. An external BIOS adds paired bus/interaction and
generated-cartridge groups. Native groups
have ten-second deadlines; generated cartridge groups have thirty seconds.
The headless runner can start a probe at `--start-address` (an aligned ARM
cartridge address) and record motor edges with `--dump-rumble`. The output
starts with `CPDRMB01`, followed by twelve-byte records: a little-endian
64-bit emulated clock, a one-byte active flag and three reserved zero bytes.
Registration and removal also report the current/stopped output. Explicit
probe entry does not exercise the BIOS boot sequence. See
[cartridge peripherals](../../hardware/cartridge/peripherals.md) for coverage
and physical measurement limits.

Cartridge minute-interrupt regressions check held and steady RTC outputs,
ARM/Thumb control writes, the inverted board edge, IF/IME/HALT sequencing,
timer/DMA ordering, late dispatch, clock wrap and reset. Calendar tests compare
bulk advances with every second; battery and frontend tests preserve the output
latch and avoid replaying IRQs from offline time. These are documented signal
model checks. Physical pulse timing and the remaining cartridge modes are
unqualified; see [RTC behavior](../../hardware/cartridge/rtc.md).

## Save file ownership

`gba.save-file` checks complete replacements and reloads at every supported
capacity, inferred expansion, explicit type choices, invalid inputs, companion
files and failed writes. Linux uses a temporary file-size resource limit to
exercise a real delayed write failure; the previous save must remain intact and
dirty state must survive. Windows checks replacement while another reader holds
the destination. Fixtures are created in an exclusive test directory and only
those owned files are removed. Failed tests retain their directory for inspection.

## RTC state and interrupts

Set `CUPID_RTC_STATE_ROM` to add `rom.rtc-state`, an optional original RTC
state/status corpus observed through a private RAM text transport. Adding
`CUPID_TEST_BIOS` also registers `rom.rtc-state-external`. The observer requires
the exact prepared input checksum, a complete EWRAM snapshot, the original
360-frame interval and the complete passing result digest for all 17 comparisons
after the presence/write guard. It retains the 150-million-step and 90-second
process limits. The original wire code, delays and comparisons are unchanged;
the cartridge and transport inputs are not fetched or distributed. These cases
cover basic state/status protocol behavior, not calendar or interrupt timing.

`gba.rtc-outputs` checks the documented alarm and selected-frequency model:
all 16 divider clocks, mixed masks across the complete divider phase, output
priority, both hour formats, held alarm output, daily carries and long waits.
`gba.rtc-irq`, `gba.rtc-battery` and `platform.rtc-config` cover parameter stores,
interrupt visibility, repeated edges across timer/DMA work, late dispatch,
clock wrap, reset, battery formats and offline state. These tests preserve
the model's specified behavior; physical cartridge edge/phase timing still
needs qualification. See [RTC behavior and limits](../../hardware/cartridge/rtc.md).

`gba.rtc-minute` checks acknowledgement and disable/enable on both sides of
the retained 10 ms carry signal, both minute modes, mode priority, power flags,
clock partitioning and fresh-zero versus actual-carry state. GPIO/IRQ groups
complete ARM/Thumb control bytes at the original strobe deadline during late
dispatch and clock wrap. Battery tests cover version 3 carry retention and
validation, legacy files and offline load without past requests.

`gba.rtc-stop` checks explicit battery time during STOP, frozen system
peripherals, release-edge wake eligibility, resumed IRQ stages, calendar and
clock wrap, SWI return in ARM/Thumb mode and paced-interval equivalence.
Supplying `CUPID_TEST_BIOS` adds the external-firmware path. `rom.rtc-stop`
generates a CupidGBA cartridge that configures the RTC over GPIO, enters STOP
and checks a frozen timer after wake. It uses the replacement BIOS. The runner
reports elapsed battery clocks separately and includes them in its existing
observation budget. These checks do not measure physical oscillator recovery.

## Tilt

`gba.tilt` checks complete X/Y samples, command and input ownership, ARM/Thumb
access widths, save isolation, EEPROM transfers, board metadata and command
strobe timing across scheduler wrap. `rom.cartridge-tilt` requires eight
generated drivers to finish all 32 paired observations each, plus disabled
device and invalid input controls. `CUPID_TEST_BIOS` adds
`rom.cartridge-tilt-external`. These use explicit guest entry and initialize
CPU modes and stacks before invoking firmware; they do not qualify BIOS boot.
Platform checks exercise manual tilt input. See the
[sensor model and unqualified timing](../../hardware/cartridge/tilt.md).

## Calendar drivers and parameter streams

The four `rom.rtc-calendar-*` groups require complete ARM/Thumb calendar drivers
through both GPIO access widths and all three ROM windows. Independent expected
records cover all one hundred years, month ends, both hour formats, weekday and
minute/second fields. Each of 3,072 records requires all eighteen returned bytes,
complete table/output pointers and a passing terminal. Missing-device and
wrong-expectation controls must finish with their exact expected failure counts.
Separate guest trials poll through a complete RTC second and check the full
year and weekday carry, coupling wire encoding to calendar advancement.
`CUPID_TEST_BIOS` repeats all four groups. Their thirty-second CTest deadlines,
twenty-second process limits and five-million-step/120-frame guards are fixed.
These are explicit-entry drivers; they do not boot firmware or qualify partial
or active-output date writes. See [RTC limits](../../hardware/cartridge/rtc.md).

`-DCUPID_TEST_JOBS=2` runs each calendar group's eight independent cartridge
cases with at most two isolated processes. Each case has its own input, output
and working directory. Completed records are reported in the original case
order. Every calendar byte, header, terminal and negative-control result is
checked. The whole group retains thirty seconds, and each process retains its
twenty-second, five-million-step and 120-frame guards. The checker also accepts
`--jobs 2` directly; its default remains serial. Clang's full sanitizer job uses
two processes, while GCC and Windows keep the default serial coverage.

`gba.rtc-parameters-component` observes every sixteen-bit parameter payload
through both command directions, including each partial word, repeated levels,
released input and chip-select abort. `gba.rtc-parameters-bus` checks ARM/Thumb
halfword/word stores in all three GPIO windows, frequency-release IRQ deadlines,
late dispatch, wrap and DMA strobes alongside a timer. Bulk and single-clock
dispatch must agree. `rom.rtc-parameters` runs six owned guest drivers and
requires all 32 IRQ observations per driver, plus all 512 serial-bit comparisons
for each nominal-read driver. `CUPID_TEST_BIOS` adds the external-firmware path.
Missing-device controls fail the positive observations. These inputs use
explicit guest entry and do not qualify BIOS boot. See the
[parameter model and physical limits](../../hardware/cartridge/rtc.md).

## Backup selection across entry points

`platform.save-config` checks the shared startup parser and all 81 selection
transitions. `runner.save-selection` runs owned guest transactions and independent
battery reloads for each concrete save device. If a BIOS is configured,
`runner.save-selection-external` repeats these checks with mapped external firmware
and an explicit cartridge entry point; it does not qualify BIOS boot.
`platform.save-selection` also runs the shipped SDL2 frontend with isolated
single-console and linked batteries. Every successful guest run must finish,
and every exported battery and response buffer is compared in full.

Invalid names, conflicting Flash options and oversized forced-capacity inputs
must fail before outputs change. Existing backup protocols, program delays,
original ROM inputs and test deadlines are unchanged. The new groups have
10-second headless/native limits and a 15-second combined frontend limit, with
three seconds per subprocess.
