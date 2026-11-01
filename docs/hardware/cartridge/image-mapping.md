# Cartridge images and EEPROM selection

CupidGBA records the supplied image's byte length separately from its mapped ROM extent. Mapping padding and existing mirroring do not change the EEPROM address decoder.

For an image no larger than 16 MiB, the supported EEPROM port spans `0x0D000000` through `0x0DFFFFFF`. Larger images reserve `0x0DFFFF00` through `0x0DFFFFFF`. A selected EEPROM responds only inside that port. Outside it, halfword and word reads retain ROM data or the existing address-derived fallback; writes leave the EEPROM command, payload and save detection unchanged. The same address selection applies to CPU accesses and DMA's cartridge counter and write path.

The supported serial driver uses incrementing DMA3 halfword transfers. Regressions write complete 512-byte and 8-KiB EEPROM commands, then read back all four dummy bits and 64 payload bits. They check both image sizes, excluded-ROM writes and reads that must not consume a pending EEPROM response. ARM/Thumb loads and all 256 bytes of the large-image port are also covered. Byte and word serial-wire behavior, physical programming delays and other cartridge aliases remain unqualified; this change preserves their existing behavior.

## Image ownership and replacement

The loader reads a complete candidate and prepares its backup allocation before
changing the attached cartridge. Missing, nonregular, empty and oversized inputs
are rejected. Open, seek, allocation, short-read, trailing-byte, read-error and
close failures leave the previous image, board, battery, scheduled programming
operation and CPU mapping intact. The regular-file check rejects an ordinary
pipe before opening it; it is not a guarantee against concurrent hostile
filesystem changes. Candidate storage temporarily adds a 32 MiB allocation and
the new backup capacity.

A successful replacement owns fresh backup storage, including when the same
image is loaded again. It redetects the device, fills allocated battery bytes
with `FFh`, clears dirty/forced selection and command state, and restores the
default timing profile. Old programming callbacks are cancelled before their
storage is freed. New programming events point to the new device and the
console scheduler. Resetting the console is different: reset retains the
attached battery bytes.

Flush the previous battery before replacing a cartridge. The loader does not
write a save file or silently carry one cartridge's battery into another.
Configure an explicit save type/profile and load the new battery after loading
the image. For compatibility, explicit device selection made before the first
image is retained. Custom programming durations configured before that first
image are also retained. Neither setting carries into a later replacement.

The previous board's motor stops while its old identity and storage are still
valid for the output callback. Replacement cancels old GPIO-prefetch and RTC
interrupt events, preserves the output subscriber and sampled sensor inputs,
installs the original image size and clears stale header identification. An
active ROM cache is rebound to the new allocation. Existing padding and
mirroring are unchanged.

Replacing an image while a CPU instruction, DMA transfer or bus transaction is
in flight returns `EBUSY` without changing the cartridge. Finish or reset that
work first. Reset the console before executing the newly attached image; image
loading itself does not restart the CPU or silently reset unrelated devices.

Detection examines word-aligned save-library markers within the original file
extent. A complete marker at EOF is valid without extra padding. Truncated,
unaligned and headerless marker fragments do not select a device.

## Coverage and limits

The four original image groups retain decoder boundaries, complete DMA serial
commands/readback, the exact 16 MiB boundary, mapping padding and CPU-cache
replacement checks. Five ownership groups add all 49 detected-device
transitions and 49 explicit-device replacement cases, same-image reloads,
initial configuration, battery-preserving
reset, pending/new programming callbacks, marker boundaries, busy/nonregular
input and board output/event ownership. A separate failure adapter compiles the
production loader and detector with failed allocation and file operations. Its
twelve initial/replacement failures must preserve complete memory, cartridge,
scheduler and active-cache state; successful controls still commit the image.
All ten groups retain ten-second deadlines.

These tests qualify the described software model, not complete cartridge or
peripheral accuracy. ROM images remain limited to 32 MiB; larger paged boards
and card-reader hardware are not implemented. Physical EEPROM framing and
programming delays remain separate qualification work.
