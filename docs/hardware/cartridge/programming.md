# Save programming and ready polling

Accepted EEPROM and Flash operations retain their old saved bytes until an
owned scheduler event reaches the operation's completion clock. Completion
publishes the entire operation and marks the saved image dirty. Reads and
additional commands cannot shorten the pending interval.

EEPROM collects all 64 payload bits before its stop bit accepts an eight-byte
program operation. A partial payload does not program storage. While busy,
reads return zero and writes are ignored. After completion, an idle read
returns one and a new read or write command can begin. Address and capacity
selection follow [EEPROM address decoding](eeprom.md).

Flash byte programming retains its selected bank and address. Sector erase
retains a 4-KiB range in the selected bank; chip erase covers the complete
selected image. With the default profile, polling the affected range returns complemented final DQ7,
alternating DQ6, and a clear DQ5 error bit. Reads outside that range retain
their stored bytes. Commands during a pending operation are ignored. The
model does not simulate programming failure, wear or erase suspension.

## Timing profiles

The default profile uses these deterministic estimates:

| Operation | GBA clocks |
| --- | ---: |
| EEPROM eight-byte program | 108368 |
| Flash byte program | 650 |
| Flash sector erase | 30000 |
| Flash chip erase | 30000 |

The EEPROM value represents a documented approximate programming interval.
Flash byte and sector values are estimates; chip erase provisionally uses the
same erase profile. They are not measured calibrations of the advertised chip
identifiers. Temperature, device variation and chip-specific timing remain
unqualified. The source models disagree on durations and on which operations
have a pending interval.

`cupid_savedata_set_programming_clocks` accepts separate durations for all four
operations. It rejects zero values, deadlines beyond the scheduler's signed
clock horizon, and changes during an accepted operation. A selected profile
changes future operations; it cannot move an operation already in flight.

### SST39VF512

`cupid_savedata_set_flash_profile(save, FLASH_PROFILE_SST39VF512)` selects a
64-KiB device reporting manufacturer/device bytes `BF D4`. Selection requires
an allocated, idle Flash512 device with no command sequence in progress.
Rejected selections leave its complete state unchanged. The SDL2 frontend
selects this device with `-S sst39vf512`; the headless runner accepts
`--flash-profile sst39vf512`.

| Operation | Nominal typical interval | GBA clocks, rounded up |
| --- | ---: | ---: |
| Byte program | 14 microseconds | 235 |
| 4-KiB sector erase | 18 milliseconds | 301990 |
| Chip erase | 70 milliseconds | 1174406 |

These values and the `BF D4` identity come from the
[device data sheet, S71150-14-000](https://ww1.microchip.com/downloads/en/DeviceDoc/S71150.pdf),
pages 1–3 and 7. They model typical operation durations, without claiming a
measured delay or the slowest permitted part. They do not calibrate the default
profile's other chip identifiers.

During an accepted operation, reads throughout the chip return completion DQ7
and alternating DQ6. Other output bits use a deterministic zero value; they
are not qualified electrical outputs. Writes, including ID exit, cannot abort
the operation. An idle ID exit accepts either a single `F0` at any address or
the three-write unlock/exit sequence. After a byte-program command, `F0` is
payload data. The device has no bank-switch command. A sector erase accepts
any address within the selected sector, including `5555`.

Selection fixes the 64-KiB capacity, so an oversized save file fails before
replacing data or cancelling an operation. Console reset, same-type selection
and compatible save replacement retain the profile. Changing the save-device
type restores its default profile. Selecting `FLASH_PROFILE_DEFAULT` restores
the default Flash identity and durations while retaining the capacity override
and EEPROM duration. The explicit programming-clock API can still select
synthetic durations for future operations without changing the device identity.

The data sheet requires an erased target sector before byte programming. Tests
use valid erased targets; programming an unerased target, physical failures,
ID aliases and malformed command behavior remain unqualified. The model
publishes all data at the ready edge. It does not simulate the asynchronous
output transition or the specified subsequent whole-byte settling interval of
up to one microsecond.

## Clock and persistence ownership

Normal CPU and DMA access clocks drive the shared scheduler. Late dispatch
retains the original ready edge and peripheral ordering across clock wrap.
Standalone save devices use explicit elapsed clocks through the same completion
path. No read advances a programming clock.

Console reset rebases an accepted operation's remaining interval when the
system scheduler returns to zero. Its captured data, range and bank survive.
During STOP, paced execution advances cartridge programming without advancing
CPU, timer, DMA, video, serial or audio clocks. Completion does not wake the CPU
or fabricate a frame. The headless runner charges stopped time against its
existing clock budget and reports that time separately.
When an RTC interrupt releases STOP before the requested interval ends,
programming consumes only the elapsed portion accepted by the cartridge.

File writes persist committed bytes. A pending operation does not become
dirty until it completes. Failed file reads retain the operation and its
deadline. Successful file replacement cancels the old operation and command
stream before replacing the image. Device replacement and destruction remove
the event before freeing its storage. See [save files](save-files.md).

Native groups cover stop-bit acceptance, before/at/after ready edges, both
Flash banks and operation ranges, polling, busy command suppression, timer/DMA/
IRQ ordering, ARM/Thumb bus strobes, clock wrap, reset, STOP, profile validation,
clock rebinding and complete file images through failures and replacement.
An owned ARM polling driver runs from IWRAM and ROM with the default byte
duration and requires a passing terminal result after the ready edge.
Their short synthetic durations test scheduler behavior independently of the
default estimates. Electrical polling aliases, malformed or interrupted wire
transactions, chip-specific durations and physical power-loss behavior remain
outside those checks. Complete cartridge accuracy remains open under
[issue 1](https://github.com/cupidthecat/cupidgba/issues/1).

Additional groups cover profile selection and complete rejection state, both
ID exits, capacity, nominal ready edges, chip-wide polling, valid sector
addresses, actual ARM/Thumb command strobes, timer ordering, reset and complete
file images. An owned guest driver checks the selected identity and polls a
byte program; default-device and deliberately wrong-ID controls must fail.
The bounded SDL2 local-session check verifies both complete Flash images,
single-console battery isolation and oversized-file rejection in both startup
paths. It does not run the unbounded single-console frontend through gameplay.
The RTC/STOP group covers 192 scenes across early, coincident and late wake
edges, clock wrap, both IME states and dispatch partitions. Its short durations
exercise event ownership and do not measure a physical chip.

Two additional original Flash cartridge probes compare data immediately after
programming without waiting for readiness. They pass the instantaneous model
and fail at that comparison with the pending-operation profile. Their original
inputs, comparisons and budgets are retained. This difference is unresolved
source-test qualification, not a passing hardware result. See
[issue 62](https://github.com/cupidthecat/cupidgba/issues/62).

A separate original EEPROM software test polls for readiness after each write.
Its initial storage, neighboring-block, address-mirror and 1,023-block stress
comparisons pass with both BIOS paths. Another original Flash software test
starts erase before its preceding byte program becomes ready; its initial
polling loop does not finish with this profile. These inputs are not
hardware-calibrated tests. Their original programs and comparisons remain
unchanged, and their results do not establish measured chip timing.

`CUPID_EEPROM_PROBE_ROM` enables the EEPROM software test when a local original
input is available. The observer requires its checksum, terminal loop, display
mode and every pixel in the complete passing result rows. It retains a fixed
1,200-frame, 150-million-step and 90-second process budget for this separate
corpus. The input is not downloaded or distributed by CupidGBA. An external
BIOS adds the same complete check through the firmware boot path.

The `gba.save-programming-reboot` group adds 32 complete warm-reboot cases:
EEPROM and Flash byte/sector/chip operations, both caller instruction states,
and four clock epochs including signed/unsigned wrap. Selected long synthetic
profiles keep programming in flight after the firmware reaches cartridge
entry. Storage, bank/range, dirty state and the original deadline survive; the
operation commits exactly at that deadline. The group retains its ten-second
deadline and does not calibrate physical chip timing.
