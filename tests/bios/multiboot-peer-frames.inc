/**
 * @file      multiboot-peer-frames.inc
 * @author    cupidthecat
 * @brief     Expected peer rejection and readiness observations.
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
/* Independent rejection and readiness observations for owned peers. */
static const struct DownloadExpected peerDownloadExpected[] = {
    {UINT64_C(1238432), UINT64_C(0xC74038BDA89847DF), 0x7863},
    {UINT64_C(1054903), UINT64_C(0xDFFCDC0E17AE031F), 0x6FE9},
    {UINT64_C(1243873), UINT64_C(0xA4285C882E43AF02), 0x7863},
    {UINT64_C(374), UINT64_C(0xCBF29CE484222325), 0xC387},
    {UINT64_C(1054903), UINT64_C(0xCBF29CE484222325), 0xC387},
};
