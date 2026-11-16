/**
 * @file      music-progress-frames.inc
 * @author    cupidthecat
 * @brief     Expected music-player progression observations.
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
/* Independent bus-progress and retained-image observations. */
static const struct MusicProgressResult musicProgressExpected[3][3] = {
    {
        {UINT64_C(670), UINT64_C(0x35B7AA7AD52830F4), UINT64_C(0xF8C7DA2093DA81C1)},
        {UINT64_C(1559), UINT64_C(0x81CF04C2B80CFD1D), UINT64_C(0x02CAEA94FE32E14B)},
        {UINT64_C(1616), UINT64_C(0x8030E83554DE75EB), UINT64_C(0x02CAEA94FE32E14B)},
    },
    {
        {UINT64_C(510), UINT64_C(0x3D2ADF0E2B074E53), UINT64_C(0x3907BA2431AC4641)},
        {UINT64_C(1335), UINT64_C(0xC44661798099B4B3), UINT64_C(0xBFCB7244285DC3C3)},
        {UINT64_C(1616), UINT64_C(0x8030E83554DE75EB), UINT64_C(0xBFCB7244285DC3C3)},
    },
    {
        {UINT64_C(670), UINT64_C(0x35B7AA7AD52830F4), UINT64_C(0xF8C7DA2093DA81C1)},
        {UINT64_C(1559), UINT64_C(0x81CF04C2B80CFD1D), UINT64_C(0x02CAEA94FE32E14B)},
        {UINT64_C(1616), UINT64_C(0x8030E83554DE75EB), UINT64_C(0x02CAEA94FE32E14B)},
    },
};
