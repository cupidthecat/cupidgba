/**
 * @file      music-sequence.inc
 * @author    cupidthecat
 * @brief     Owned music sequence inputs and sequencer observations.
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
/* Independent captures of the owned song, wave and input sequence. */
struct MusicExpected {
    uint64_t clocks, signature;
};
static const struct MusicExpected musicSequenceExpected[18] = {
    {UINT64_C(308), UINT64_C(0x090F72C46D562D07)},
    {UINT64_C(320), UINT64_C(0x30BEEE5E23A574F7)},
    {UINT64_C(18553), UINT64_C(0xC527191CA82A876B)},
    {UINT64_C(16999), UINT64_C(0x16BD5BE936CC494B)},
    {UINT64_C(16961), UINT64_C(0x9CCB994251E90030)},
    {UINT64_C(16978), UINT64_C(0x8BF455AB6FACA382)},
    {UINT64_C(17128), UINT64_C(0x3DAB5E59A1DC743C)},
    {UINT64_C(16895), UINT64_C(0xC4DD8055F7A87C68)},
    {UINT64_C(16868), UINT64_C(0x3BA146D72F60658A)},
    {UINT64_C(16892), UINT64_C(0xC01FB4926ABA1922)},
    {UINT64_C(16895), UINT64_C(0xF7F2898E245837E0)},
    {UINT64_C(16877), UINT64_C(0x70918C8489DB560F)},
    {UINT64_C(16893), UINT64_C(0xCB7739543107F973)},
    {UINT64_C(1659), UINT64_C(0x2B9868B3A80F5C2A)},
    {UINT64_C(208), UINT64_C(0xBBED3E0E898AC6F3)},
    {UINT64_C(88), UINT64_C(0xFD15892DB86E7873)},
    {UINT64_C(90), UINT64_C(0xA82BBA0FFC2D1194)},
    {UINT64_C(1422), UINT64_C(0xA82BBA0FFC2D1194)},
};
