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
