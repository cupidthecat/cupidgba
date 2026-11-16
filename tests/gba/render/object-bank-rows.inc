/**
 * @file      object-bank-rows.inc
 * @author    cupidthecat
 * @brief     Expected eight-pixel object texture rows at bank boundaries.
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
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Complete eight-pixel rows for the owned bank-boundary texture. */
static const uint16_t bankRows[2][2][3][8] = {
    {
        {
            {0x4469, 0x9614, 0x9000, 0x9119, 0x6696, 0x9410, 0x9696, 0x1114},
            {0x4940, 0x4604, 0x4609, 0x9110, 0x9091, 0x6600, 0x4141, 0x9006},
            {0x6919, 0x1061, 0x0019, 0x4446, 0x9996, 0x4101, 0x1164, 0x1440},
        },
        {
            {0x4469, 0x9614, 0x9000, 0x9119, 0x6696, 0x9410, 0x9696, 0x1114},
            {0x4940, 0x4604, 0x4609, 0x9110, 0x9091, 0x6600, 0x4141, 0x9006},
            {0x6919, 0x1061, 0x0019, 0x4446, 0x9996, 0x4101, 0x1164, 0x1440},
        },
    },
    {
        {
            {0x66A9, 0x5540, 0x64A6, 0x5666, 0xA281, 0x9A65, 0x8912, 0x4406},
            {0xA298, 0x54A1, 0xA045, 0x4299, 0x4995, 0xAA05, 0x9156, 0x685A},
            {0xA298, 0x54A1, 0xA045, 0x4299, 0x4995, 0xAA05, 0x9156, 0x685A},
        },
        {
            {0x66A9, 0x5540, 0x64A6, 0x5666, 0xA281, 0x9A65, 0x8912, 0x4406},
            {0xA298, 0x54A1, 0xA045, 0x4299, 0x4995, 0xAA05, 0x9156, 0x685A},
            {0x4995, 0xAA05, 0x9156, 0x685A, 0x66A9, 0x5540, 0x64A6, 0x5666},
        },
    },
};
