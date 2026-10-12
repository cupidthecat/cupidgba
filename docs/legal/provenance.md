# CupidGBA licensing

CupidGBA project contributions are licensed under GPL-3.0-or-later. The full
license is in [LICENSE](../../LICENSE).

CupidGBA's source notices are collected in [LICENSE](../../LICENSE). Additional
license texts are available in [licenses/MPL-2.0.txt](licenses/MPL-2.0.txt),
[licenses/timer-BSD-2-Clause.txt](licenses/timer-BSD-2-Clause.txt), and
[licenses/timer-BSD-3-Clause.txt](licenses/timer-BSD-3-Clause.txt).

The built-in replacement BIOS has assembly source at
[src/gba/bios/hle-bios.s](../../src/gba/bios/hle-bios.s). See
[rebuilding the replacement BIOS](../development/rebuilding-bios.md) for the
commands used to verify its embedded bytes. External BIOS dumps and game ROMs
are separate user inputs and are not covered by the project license.

CupidGBA's optional test inputs are pinned in the test manifests. The test
scripts download them into an ignored directory and verify their SHA-256
digests. These inputs are not included in source commits. Their license texts
are in [licenses/test-roms-MIT.txt](licenses/test-roms-MIT.txt) and
[licenses/hardware-tests-BSD-3-Clause.txt](licenses/hardware-tests-BSD-3-Clause.txt).
See [testing](../development/testing.md) for download and execution commands.
