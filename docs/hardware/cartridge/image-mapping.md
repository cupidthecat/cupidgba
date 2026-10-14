# Cartridge images and EEPROM selection

CupidGBA records the supplied image's byte length separately from its mapped ROM extent. Mapping padding and existing mirroring do not change the EEPROM address decoder.

For an image no larger than 16 MiB, the supported EEPROM port spans `0x0D000000` through `0x0DFFFFFF`. Larger images reserve `0x0DFFFF00` through `0x0DFFFFFF`. A selected EEPROM responds only inside that port. Outside it, halfword and word reads retain ROM data or the existing address-derived fallback; writes leave the EEPROM command, payload and save detection unchanged. The same address selection applies to CPU accesses and DMA's cartridge counter and write path.

The supported serial driver uses incrementing DMA3 halfword transfers. Regressions write complete 512-byte and 8-KiB EEPROM commands, then read back all four dummy bits and 64 payload bits. They check both image sizes, excluded-ROM writes and reads that must not consume a pending EEPROM response. ARM/Thumb loads and all 256 bytes of the large-image port are also covered. Byte and word serial-wire behavior, physical programming delays and other cartridge aliases remain unqualified; this change preserves their existing behavior.

ROM replacement opens, sizes and reads a complete candidate before replacing the attached allocation. Open, seek, size, allocation, read or close failure leaves the previous image, decoding metadata, save data and CPU mapping intact. Success installs the candidate and its original size together, clears any previous header identifier and rebinds an active ROM execution cache. It preserves the existing image padding, mirroring and save-type policy. Candidate storage temporarily adds a 32 MiB allocation.

Four native groups cover decoder boundaries, complete DMA command/readback sequences, the exact 16 MiB boundary and mapping padding, and failed/successful replacement ownership. Each has a ten-second deadline. These qualify the described software model; complete cartridge and peripheral accuracy remains open.
