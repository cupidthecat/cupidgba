# Save files

CupidGBA stores raw save bytes in a `.sav` file next to the ROM. The supported
capacities are:

| Save type | File size |
| --- | ---: |
| EEPROM512 | 512 bytes |
| EEPROM | 8 KiB |
| SRAM | 32 KiB |
| SRAM512 | 64 KiB |
| Flash512 | 64 KiB |
| Flash1M | 128 KiB |

An inferred save type can expand within its family when a recognized file
contains the larger capacity. An explicit `-S` override keeps its selected
capacity. A smaller related file fills the unused space with erased bytes
(`0xFF`). With no type hint, file size selects the type; an ambiguous 64 KiB
file defaults to Flash512.

Loading reads into a separate buffer and checks the complete read and close
before publishing it. Invalid sizes, incompatible families and I/O failures
leave active data and protocol state unchanged. A missing file starts a new
save. Other load failures stop the frontend before emulation starts. `-S none`
ignores the sidecar file. Files with headers or trailing metadata are rejected;
they require separate format support.

Saving creates an exclusive temporary file beside the destination. It checks
the complete write, flush, host file synchronization and close before replacing
the previous file. Linux uses `rename`; Windows uses `MoveFileEx` with replacement
enabled. Failed writes or replacements retain the previous file and dirty state.
Only the temporary file created by that attempt is removed. Existing companion
files are preserved. The frontend reports save failures and exits unsuccessfully
if its final flush fails.

Destinations must be regular writable files. Links, directories and read-only
files are rejected. Existing POSIX access bits are retained as reported by the
filesystem. An open Windows file can prevent replacement; closing the other
application allows a retry. A path that exceeds the frontend's save-path buffer
is rejected rather than truncated.

Before execution, the frontend also checks derived save and enabled RTC paths
against every ROM, BIOS, card and replay input, and against the other consoles'
output paths. A collision stops startup without replacing any file. Shared
read-only ROMs retain independent player battery paths. See
[input and output ownership](../../usage/files.md) for explicit runner writeback
and capture-path rules.

Native regressions cover all capacities, two complete replacements and reloads,
capacity expansion, explicit overrides, invalid input, companion-file collisions,
protected files, open readers and recovery after a real delayed write error.
Frontend checks verify invalid-save rejection and filename handling. They also
cover bounded single-console runs, complete SRAM reloads across three
processes, periodic and final flushes, early quit, STOP with an RTC battery, and
read-only destination failures. The interval observer retains the real core,
SDL input/render/audio and battery paths while removing only host pacing.
See [running games](../../usage/running.md) for `--frames`. These checks qualify
file persistence. Chip-specific EEPROM/Flash durations, disputed bus accesses,
peripheral metadata and complete cartridge accuracy remain in
[issue 1](https://github.com/cupidthecat/cupidgba/issues/1). Power-loss behavior
beyond the host's file and replacement guarantees remains unqualified.

[EEPROM address decoding](eeprom.md) describes command-width capacity inference
and block selection before data reaches the saved image.

[Save programming](programming.md) describes pending operations, ready polling
and the committed bytes available to file persistence.

## Allocation failures

Backup initialization and explicit type changes allocate replacement storage
before releasing the attached device. A failed allocation preserves its bytes,
capacity, selected profile, dirty flag, command decoder and pending programming
event. The allocating APIs return `false` with `errno=ENOMEM`; invalid forced
types return `false` with `errno=EINVAL`. Startup configuration propagates failure
to its caller. Rejected profile changes also preserve the previous selection.

Flash bank expansion preserves the complete smaller device if allocation fails.
If the first Flash unlock write cannot allocate storage, the save bus remembers
the observed family while the public type remains undetected and storage remains
absent. Later save-bus reads or writes retry Flash allocation. They process only
the new access; the rejected unlock write is never replayed, so a continued
unlock sequence must be restarted before it can program data. EEPROM traffic and
ROM signature inference cannot take ownership while Flash allocation is pending.
Reset, deinitialization and an explicit type change clear this host recovery
state; a failed type change preserves it. The frontend's `auto` selection retains
the pending family, just as it retains an already allocated device.

An attempted EEPROM capacity expansion rejects the current serial frame on
failure, retaining the existing bytes and capacity. Its remaining bits are
discarded through the original transfer's stop boundary. The same rule applies
when initial EEPROM detection cannot allocate storage, so later payload bits
cannot become a new command. A fresh complete transfer can retry after memory
becomes available. Reset also clears the discarded-frame state.

These are host allocation recovery rules. They do not add chip failure timing or
change the normal programming deadlines. Fault-injection checks use the
production save, memory and DMA paths, including pending events and successful
retries; the original hardware and file-persistence expectations remain required.

## Consistent startup selection

The frontend and headless runner share the backup-name parser and startup
configuration. An explicit selection is applied after loading the cartridge
and before reset and battery restoration. `auto` leaves the detected device
alone; `none` disables backup storage. Unknown names are rejected rather than
silently falling back to a different device. The hardware protocol, programming
durations and raw file layouts are unchanged.

Native checks cover all 81 prior-device/selected-device pairs, exact names,
invalid configurations without state changes, capacity, retained bytes and Flash
profile selection. Owned ARM cartridges exercise SRAM, both Flash capacities,
the explicit Flash profile and both EEPROM capacities. EEPROM commands use DMA3
from internal RAM and read all 68 response bits. Flash1M writes and reads bank 1;
SRAM512 accesses its upper half. Host checks compare the entire battery and
EWRAM response, then reload the output in a separate read-only guest process.
The frontend runs the same writes through single-console and two-console paths.
These checks establish selection and persistence, not new chip timing evidence.
