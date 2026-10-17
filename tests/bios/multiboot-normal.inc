/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Independent full wire-transcript observations for the owned downloads. */
struct DownloadExpected { uint64_t clocks, signature; uint32_t crc; };
static const struct DownloadExpected downloadExpected[] = {
    {UINT64_C(1238445), UINT64_C(0xC73D32BDA89607D6), 0x7863},
    {UINT64_C(1206314), UINT64_C(0xC2909847AA12394C), 0x8BE5},
    {UINT64_C(1237542), UINT64_C(0xCD20DBBDB1144B10), 0x7863},
    {UINT64_C(1344613), UINT64_C(0xED580AECB7870FC4), 0xBCE7},
};
