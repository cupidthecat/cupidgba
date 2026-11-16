/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      internal.h
 * @author    cupidthecat
 * @brief     Internal card format conversion and camera scan helpers.
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
#ifndef CUPID_CARD_READER_INTERNAL_H
#define CUPID_CARD_READER_INTERNAL_H

#include "cupid/card-reader.h"

bool cupid_card_decode(const uint8_t *data, size_t size, uint8_t dots[CUPID_CARD_DOTS]);
void cupid_card_parity(const uint8_t *data, size_t size, uint8_t parity[16]);

#endif
