/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      paged-rom.h
 * @author    cupidthecat
 * @brief     Paged cartridge backing storage and window-control interface.
 * @date      2026-11-16
 *
 * - Retain controller selection alongside the complete image backing.
 * - Declare controller reset and register-write handling.
 *
 * @copyright Copyright (c) 2026 cupidthecat
 *
 * This file is part of CupidGBA.
 *
 * CupidGBA is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CupidGBA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CupidGBA.  If not, see <https://gnu.org>.
 */
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
