/**
 * @file      multiboot-multi-frames.inc
 * @author    cupidthecat
 * @brief     Expected complete multiplayer download wire transcripts.
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
/* Independent full multiplayer wire-transcript observations. */
static const struct DownloadExpected multiDownloadExpected[] = {
    {UINT64_C(1905886), UINT64_C(0xCA53A6B3BFEEE02F), 0xCF01},
    {UINT64_C(2536763), UINT64_C(0x647B72E80CAE562F), 0x32DC},
    {UINT64_C(3413685), UINT64_C(0x30F8DB8E2F300C49), 0xF546},
    {UINT64_C(2536629), UINT64_C(0xEFC838CD2EA06AE1), 0x32DC},
};
static const struct DownloadExpected multiRejectionExpected[] = {
    {UINT64_C(1076450), UINT64_C(0x8C2EB8E6090FE522), 0x962E},
    {UINT64_C(3413672), UINT64_C(0x2C057F998EADEA64), 0xF546},
    {UINT64_C(2536629), UINT64_C(0x6E63419046772688), 0x32DC},
};
