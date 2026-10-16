/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "music-callbacks.inc"

static uint32_t music_hook_address(unsigned hook, bool thumb) {
    return 0x02018000 + (thumb ? 0x401 : 0) + hook * 32;
}
static void music_hooks_prepare(struct CupidGBA* gba, bool thumb) {
    uint8_t* sound = ram(gba, soundAddress);
    memcpy(gba->memory.ewram + 0x18000, musicCallbacks, sizeof(musicCallbacks));
    memset(gba->memory.ewram + 0x5000, 0, 256);
    memset(gba->memory.ewram + 0x3500, 0, 256);
    uint32_t entries[36]; read_entries(gba, entries);
    memcpy(gba->memory.ewram + 0x16000, entries, sizeof(entries));
    put(gba->memory.ewram, 0x18F00, word(sound, 56));
    put(gba->memory.ewram, 0x18F04, entries[13]);
    put(gba->memory.ewram, 0x16000 + 13 * 4, music_hook_address(5, thumb));
    put(sound, 28, 0x02005000);
    put(sound, 40, music_hook_address(0, thumb));
    put(sound, 44, music_hook_address(2, thumb));
    put(sound, 48, music_hook_address(1, thumb));
    put(sound, 52, 0x02016000);
    put(sound, 56, music_hook_address(4, thumb));
    put(sound, 60, music_hook_address(3, thumb));
    music_scene_header(gba, 0x8000, 4, 7, 0, 0x9000);
    memset(gba->memory.ewram + 0xC000, 0, 256);
    for (unsigned track = 0; track < 4; track++) {
        music_scene_tone(gba, 0xC000 + track * 12, track + 1, 60, 64, 0x0200D000);
        const unsigned offset = 0x9000 + track * 0x200;
        const uint8_t script[] = {0xBD, (uint8_t)track, 0xBE, 120, 0xCF,
            (uint8_t)(60 + track * 2), 100, 0x81, 0xC0, 68, 0x81, 0xBE, 80,
            0xBF, 0, 0x81, 0xCE, 0x81, 0xB2, 0, 0, 0, 0};
        memcpy(gba->memory.ewram + offset, script, sizeof(script));
        put(gba->memory.ewram, offset + 19, 0x02000000 + offset + 4);
    }
    open_player(gba, 4);
    invoke(gba, 0x21, playerAddress, 0x02008000, 0x11220002, thumb, MODE_SYS);
}
static void music_hooks_event(struct CupidGBA* gba, unsigned frame) {
    if (frame == 6) invoke(gba, 0x22, playerAddress, 0x11220001, 0x11220002, false, MODE_SYS);
    if (frame == 7) invoke(gba, 0x23, playerAddress, 0x11220001, 0x11220002, true, MODE_SYS);
    if (frame == 8) invoke(gba, 0x21, playerAddress, 0x02008000, 0x11220002, false, MODE_SYS);
}
