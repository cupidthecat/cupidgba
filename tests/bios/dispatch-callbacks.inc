/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      dispatch-callbacks.inc
 * @author    cupidthecat
 * @brief     Generated owned ARM callback and interrupt-handler words.
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
static const uint32_t plain[] = {
    0xE8807FFF, 0xE10F1000, 0xE580103C, 0xE3A01000, 0xE5911000, 0xE5801040, 0xE3A00011,
    0xE3A01022, 0xE3A03033, 0xE3A02066, 0xE3A0B077, 0xE3A0C088, 0xE12FFF1E,
};
static const uint32_t nested[] = {
    0xE8807FFF, 0xE10F1000, 0xE580103C, 0xE3A01000, 0xE5911000, 0xE5801040,
    0xE92D4000, 0xE3A00011, 0xE3A01003, 0xEF060000, 0xE59FC014, 0xE88C000F,
    0xE8BD4000, 0xE3A02066, 0xE3A0B077, 0xE3A0C088, 0xE12FFF1E, 0x02001300,
};
static const uint32_t dma[] = {
    0xE8807FFF, 0xE10F1000, 0xE580103C, 0xE3A01000, 0xE5911000, 0xE5801040, 0xE59F1030, 0xE59F2030,
    0xE5812000, 0xE59F202C, 0xE5812004, 0xE59F2028, 0xE5812008, 0xE3A00011, 0xE3A01022, 0xE3A03033,
    0xE3A02066, 0xE3A0B077, 0xE3A0C088, 0xE12FFF1E, 0x040000D4, 0x08000100, 0x02001400, 0x84000004,
};
static const uint32_t timer[] = {
    0xE8807FFF, 0xE10F1000, 0xE580103C, 0xE3A01000, 0xE5911000, 0xE5801040, 0xE59F102C,
    0xE59F202C, 0xE5812000, 0xE3A03040, 0xE2533001, 0x1AFFFFFD, 0xE3A00011, 0xE3A01022,
    0xE3A03033, 0xE3A02066, 0xE3A0B077, 0xE3A0C088, 0xE12FFF1E, 0x04000100, 0x00C0FFF0,
};
static const uint32_t handler[] = {
    0xE59F0028, 0xE3A01000, 0xE1C010B2, 0xE3A00301, 0xE2800C02, 0xE3A01008, 0xE1C010B2,
    0xE59F0010, 0xE5901000, 0xE2811001, 0xE5801000, 0xE12FFF1E, 0x04000100, 0x02001200,
};
