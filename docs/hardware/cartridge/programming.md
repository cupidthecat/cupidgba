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
selected image. Polling the affected range returns complemented final DQ7,
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
