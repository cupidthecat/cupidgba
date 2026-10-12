# License and source notices

CupidGBA project contributions are licensed under GPL-3.0-or-later. The full
license is in [LICENSE](../../LICENSE).

The existing ARM core, decode emitters, register definitions, timing scheduler,
and portions of the GBA core, memory bus, timers, audio, video, I/O, save hardware,
and BIOS implementation contain code adapted from mGBA. These portions retain
their original copyright notices and MPL-2.0 source terms. The MPL license is in
[licenses/MPL-2.0.txt](licenses/MPL-2.0.txt). The combined project is distributed
under GPL-3.0-or-later with those source notices and terms preserved.

The notices were checked against mGBA revision
`143adf5aa2a60c6036810b0cd72445e13d74b0e6` from
<https://github.com/mgba-emu/mgba>. In particular, the embedded HLE BIOS byte
array matches that revision's `src/gba/hle-bios.c`; its preferred assembly source
is included at [src/gba/bios/hle-bios.s](../../src/gba/bios/hle-bios.s).
This is an open source replacement BIOS. External BIOS dumps and commercial
game ROMs are separate user inputs and are not covered by the project license.

Optional test inputs come from <https://github.com/jsmolka/gba-tests> at revision
`a7113b67e63f83a9b321696ddd7042ccfad6c881`. Their MIT copyright and license notice
is included in [licenses/test-roms-MIT.txt](licenses/test-roms-MIT.txt).
The test script downloads inputs into an ignored directory and checks their
SHA-256 digests. These inputs are not included in source commits.
