/**
 * @file      multiboot-normal.inc
 * @author    cupidthecat
 * @brief     Expected complete normal-mode download wire transcripts.
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
/* Independent full wire-transcript observations for the owned downloads. */
struct DownloadExpected {
    uint64_t clocks, signature;
    uint32_t crc;
};
static const struct DownloadExpected downloadExpected[] = {
    {UINT64_C(1238445), UINT64_C(0xC73D32BDA89607D6), 0x7863},
    {UINT64_C(1206314), UINT64_C(0xC2909847AA12394C), 0x8BE5},
    {UINT64_C(1237542), UINT64_C(0xCD20DBBDB1144B10), 0x7863},
    {UINT64_C(1344613), UINT64_C(0xED580AECB7870FC4), 0xBCE7},
};
