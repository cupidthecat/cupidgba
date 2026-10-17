/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Owned songs and waves exercise sequencing, mapping and competing players. */
static unsigned music_scene_frames(unsigned scene) { return scene == 7 ? 20 : (scene == 4 ? 16 : 12); }

static void music_scene_wave(struct CupidGBA* gba, unsigned offset, unsigned voice) {
    uint8_t* wave = gba->memory.ewram + offset;
    memset(wave, 0, 0x220); wave[3] = 0x40;
    put(wave, 4, 0x01000000 + voice * 0x80000); put(wave, 8, 32); put(wave, 12, 512);
    for (unsigned sample = 0; sample < 512; sample++) wave[16 + sample] = (uint8_t)(sample * 73 + 19 + voice * 37);
    wave[528] = wave[48];
}
static void music_scene_tone(struct CupidGBA* gba, unsigned offset, unsigned type, unsigned key,
                             unsigned pan, uint32_t wave) {
    uint8_t* tone = gba->memory.ewram + offset;
    tone[0] = (uint8_t)type; tone[1] = (uint8_t)key; tone[2] = 0; tone[3] = (uint8_t)pan;
    put(tone, 4, wave); put(tone, 8, 0x80C040FF);
}
static void music_scene_header(struct CupidGBA* gba, unsigned offset, unsigned count, unsigned priority,
                               unsigned mode, unsigned script) {
    uint8_t* song = gba->memory.ewram + offset;
    memset(song, 0, 0x100); song[0] = (uint8_t)count; song[2] = (uint8_t)priority; song[3] = (uint8_t)mode;
    put(song, 4, 0x0200C000);
    for (unsigned track = 0; track < count; track++) put(song, 8 + track * 4, 0x02000000 + script + track * 0x200);
}
static void music_scene_prepare(struct CupidGBA* gba, unsigned scene) {
    const unsigned count = scene == 1 || scene == 6 ? 2 : (scene == 5 ? 4 : 1);
    music_scene_header(gba, 0x8000, count, 7, scene == 1 ? 0xA0 : 0, 0x9000);
    memset(gba->memory.ewram + 0x9000, 0, 0x1000);
    memset(gba->memory.ewram + 0xC000, 0, 0x1000);
    memset(gba->memory.ewram + 0xE000, 0, 0x1100);
    for (unsigned voice = 0; voice < 3; voice++) {
        music_scene_wave(gba, 0xD000 + voice * 0x400, voice);
        music_scene_tone(gba, 0xC000 + voice * 12, voice & 1 ? 8 : 0, 60, 0, 0x0200D000 + voice * 0x400);
    }
    if (!scene) {
        const uint8_t script[] = {0xBB, 75, 0xBD, 0, 0xBE, 127, 0xBF, 64, 0xD4, 60, 100, 0x84, 0xB1};
        memcpy(gba->memory.ewram + 0x9000, script, sizeof(script));
    } else if (scene == 1) {
        for (unsigned track = 0; track < 2; track++) {
            const uint8_t script[] = {0xBD, (uint8_t)track, 0xBE, 120, 0xBF, (uint8_t)(track ? 127 : 0),
                0xD7, (uint8_t)(48 + track * 12), (uint8_t)(90 + track * 20), 0x84,
                0xD3, (uint8_t)(55 + track * 12), 100, 0x84, 0xB1};
            memcpy(gba->memory.ewram + 0x9000 + track * 0x200, script, sizeof(script));
        }
    } else if (scene == 2) {
        const uint8_t script[] = {0xBD, 0, 0xBE, 100, 0xD3, 60, 90, 0x81, 62, 70, 0x81,
            63, 80, 0x82, 0xBF, 0, 0x81, 127, 0x81, 0xB1};
        memcpy(gba->memory.ewram + 0x9000, script, sizeof(script));
    } else if (scene == 3) {
        const uint8_t script[] = {0xBD, 0, 0xBE, 100, 0xB3, 0, 0, 0, 0, 0xBA, 3,
            0xB5, 2, 0, 0, 0, 0, 0x81, 0xB1};
        const uint8_t pattern[] = {0xBF, 0, 0xD1, 60, 90, 0x81, 0xB4};
        const uint8_t repeat[] = {0xBE, 80, 0xD2, 64, 80, 0x81, 0xB2, 0, 0, 0, 0};
        memcpy(gba->memory.ewram + 0x9000, script, sizeof(script)); put(gba->memory.ewram, 0x9005, 0x02009051);
        put(gba->memory.ewram, 0x900D, 0x02009061);
        memcpy(gba->memory.ewram + 0x9051, pattern, sizeof(pattern));
        memcpy(gba->memory.ewram + 0x9061, repeat, sizeof(repeat)); put(gba->memory.ewram, 0x9068, 0x0200900B);
    } else if (scene == 4) {
        const uint8_t script[] = {0xBB, 150, 0xBD, 0, 0xBE, 100, 0xBF, 64, 0xC0, 64, 0xC1, 12,
            0xC2, 32, 0xC3, 1, 0xC4, 64, 0xC5, 0, 0xC8, 63, 0xD7, 60, 100, 0x84,
            0xC5, 1, 0x84, 0xC5, 2, 0x84, 0xC2, 0, 0xC4, 0, 0x84, 0xB1};
        memcpy(gba->memory.ewram + 0x9000, script, sizeof(script));
    } else if (scene == 5) {
        const uint8_t priorities[] = {10, 20, 0, 20};
        ram(gba, soundAddress)[6] = 2;
        for (unsigned track = 0; track < 4; track++) {
            const unsigned scriptOffset = 0x9000 + track * 0x200;
            const uint8_t script[] = {0xBD, 0, 0xBE, 120, 0xBA, priorities[track], 0xD1,
                (uint8_t)(48 + track * 4), 90, 0x83, 0xB2, 0, 0, 0, 0};
            memcpy(gba->memory.ewram + scriptOffset, script, sizeof(script));
            put(gba->memory.ewram, scriptOffset + 11, 0x02000000 + scriptOffset + 6);
        }
    } else if (scene == 6) {
        music_scene_tone(gba, 0xC000, 0x40, 0, 0, 0x0200E000); put(gba->memory.ewram, 0xC008, 0x0200F000);
        music_scene_tone(gba, 0xC00C, 0x80, 0, 0, 0x0200E100);
        music_scene_tone(gba, 0xE000, 0, 60, 0, 0x0200D000);
        music_scene_tone(gba, 0xE00C, 8, 60, 0, 0x0200D400);
        music_scene_tone(gba, 0xE100 + 36 * 12, 0, 55, 0xFF, 0x0200D800);
        music_scene_tone(gba, 0xE100 + 38 * 12, 8, 57, 0x80, 0x0200D400);
        gba->memory.ewram[0xF000 + 48] = 1;
        const uint8_t first[] = {0xBD, 0, 0xBE, 110, 0xD3, 48, 100, 0x82, 0xD3, 60, 100, 0x82, 0xB1};
        const uint8_t second[] = {0xBD, 1, 0xBE, 110, 0xD3, 36, 100, 0x82, 0xD3, 38, 100, 0x82, 0xB1};
        memcpy(gba->memory.ewram + 0x9000, first, sizeof(first)); memcpy(gba->memory.ewram + 0x9200, second, sizeof(second));
    } else if (scene == 7) {
        music_scene_header(gba, 0x8100, 1, 20, 0, 0x9200);
        for (unsigned track = 0; track < 2; track++) {
            const unsigned scriptOffset = 0x9000 + track * 0x200;
            const uint8_t script[] = {0xBD, (uint8_t)track, 0xBE, 120, 0xBF, (uint8_t)(track ? 127 : 0),
                0xD3, (uint8_t)(60 + track * 7), 100, 0x82, 0xB2, 0, 0, 0, 0};
            memcpy(gba->memory.ewram + scriptOffset, script, sizeof(script));
            put(gba->memory.ewram, scriptOffset + 11, 0x02000000 + scriptOffset + 6);
        }
    } else if (scene == 8) {
        /* Two indefinite notes, explicit EndTie, then running-key EndTie. */
        const uint8_t script[] = {0xBD, 0, 0xBE, 120, 0xCF, 60, 100, 0x82,
            0xCF, 64, 90, 0x82, 0xCE, 60, 0x82, 0xCE, 0x82, 0xB1};
        memcpy(gba->memory.ewram + 0x9000, script, sizeof(script));
    } else {
        /* Four nested Pattern commands reach the three-return-address limit. */
        const uint8_t script[] = {0xBD, 0, 0xBE, 120, 0xB3, 0, 0, 0, 0, 0x81, 0xB1};
        memcpy(gba->memory.ewram + 0x9000, script, sizeof(script));
        put(gba->memory.ewram, 0x9005, 0x02009040);
        for (unsigned level = 0; level < 4; level++) {
            const unsigned offset = 0x9040 + level * 0x40;
            const uint8_t pattern[] = {0xD1, (uint8_t)(60 + level), 100, 0x81,
                0xB3, 0, 0, 0, 0, 0xB4};
            memcpy(gba->memory.ewram + offset, pattern, sizeof(pattern));
            put(gba->memory.ewram, offset + 5, 0x02000000 + offset + 0x40);
        }
    }
    open_player(gba, scene == 5 ? 4 : 2);
    invoke(gba, 0x21, playerAddress, 0x02008000, 0x11220002, true, MODE_SYS);
    if (scene == 7) {
        const uint32_t previous = word(ram(gba, soundAddress), 32);
        invoke(gba, 0x20, playerAddress + 64, trackAddress + 8 * 80, 2, false, MODE_SYS);
        CHECK(word(ram(gba, playerAddress), 64 + 56) == previous);
        CHECK(word(ram(gba, playerAddress), 64 + 60) == playerAddress);
        invoke(gba, 0x21, playerAddress + 64, 0x02008100, 0x11220002, false, MODE_SYS);
    }
}
static void music_scene_event(struct CupidGBA* gba, unsigned scene, unsigned frame) {
    if (scene != 7) return;
    if (frame == 4) invoke(gba, 0x24, playerAddress, 1, 0x11220002, true, MODE_SYS);
    if (frame == 8) invoke(gba, 0x22, playerAddress + 64, 0x11220001, 0x11220002, false, MODE_SYS);
    if (frame == 10) invoke(gba, 0x23, playerAddress + 64, 0x11220001, 0x11220002, true, MODE_SYS);
}
