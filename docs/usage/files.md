# Input and output files

ROMs, BIOS images, card strips and input scripts are read-only inputs. Capture
paths must use different files from those inputs and from each other. An audio
capture cannot replace its ROM, and a screenshot and memory dump cannot share
one destination. Conflicting paths fail before execution or output creation,
preserving existing files.

The check follows existing file identities, including hard links and directory
aliases. It also compares normalized names for new files in existing
directories. Output directories must already exist. Directories, pipes and
unresolved links are not capture destinations.

Explicit battery and RTC writeback remains valid:

```sh
cupidgba-headless game.gba --load-save battery.sav --dump-save battery.sav
cupidgba-headless game.gba --rtc on --load-rtc clock.rtc --dump-rtc clock.rtc
```

Use different input and output paths to retain an unchanged seed. A save input
may share its save output, and an RTC input may share its RTC output. These
exceptions do not allow either output to overlap another input or capture.
The existing persistence checks still apply to the destination.
Raw memory and battery dumps stage and replace a complete file. A failed write
retains the previous snapshot, including an explicitly loaded battery used for
in-place writeback. These snapshot destinations must be writable regular files.

The SDL2 frontend checks its derived save and enabled RTC paths across every
console before starting a single-console or local linked session. Read-only
ROMs may be shared between consoles; each console retains its own battery path.
Disabled persistence hardware does not claim an output path.

This is a startup check. It does not lock paths against another process
renaming or replacing them while emulation runs.
