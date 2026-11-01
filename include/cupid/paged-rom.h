/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_PAGED_ROM_H
#define CUPID_PAGED_ROM_H
#include "cupid/types.h"

struct CupidGBA;

enum {
    GBA_SIZE_PAGED_ROM = 0x04000000,
    GBA_SIZE_PAGED_VIEW = 0x01000000,
    GBA_SIZE_PAGED_WINDOW = 0x2000
};

/* Immutable cartridge bytes remain separate from the CPU-visible page window. */
struct CupidPagedROM {
    uint8_t *image;
    size_t imageSize;
    uint32_t command, source, destination, length;
};

void cupid_paged_rom_reset(struct CupidPagedROM *page, uint8_t *view);
/* Return true for a consumed controller write, including unsupported commands. */
bool cupid_paged_rom_write(struct CupidGBA *gba, uint32_t address, uint32_t value, unsigned width);
#endif
