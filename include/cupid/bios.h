/**
 * @file      bios.h
 * @author    cupidthecat
 * @brief     GBA BIOS SWI (software interrupt) handling.
 * @date      2026-11-16
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
#ifndef CUPID_BIOS_H
#define CUPID_BIOS_H

#include "cupid/types.h"

struct CupidGBA;

/**
 * Enter the SWI exception for the built-in firmware. The normalized service
 * byte does not gate entry; firmware reads the caller opcode and dispatches.
 */
void cupid_bios_swi(struct CupidGBA *gba, uint8_t comment);

/** Built-in BIOS image containing reset, exception and service instructions. */
extern const uint8_t cupid_hle_bios[];

#endif /* CUPID_BIOS_H */
