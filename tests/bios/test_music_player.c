/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"
#include "caller.h"
#include "music-sequence.inc"
#include "music-track-fields.inc"
#include "music-song-frames.inc"
#include "music-hook-frames.inc"
#include "music-playback.inc"

enum { MUSIC_READY = 0x68736D53, SOUND_BYTES = 0x1000, TRACK_BYTES = 0x600 };
static const char* externalBios;
static uint32_t soundAddress, playerAddress, trackAddress;
static unsigned observations;
static bool unmaskedCaller;

static uint32_t word(const uint8_t* data, unsigned offset) {
    uint32_t result; memcpy(&result, data + offset, 4); return result;
}
static void put(uint8_t* data, unsigned offset, uint32_t value) { memcpy(data + offset, &value, 4); }
static void put_half(uint8_t* data, unsigned offset, uint16_t value) { memcpy(data + offset, &value, 2); }
static uint8_t* ram(struct CupidGBA* gba, uint32_t address) {
    CHECK(address >> 24 == 2 || address >> 24 == 3);
    return address >> 24 == 2 ? gba->memory.ewram + (address & 0x3FFFF)
                              : gba->memory.iwram + (address & 0x7FFF);
}
static uint64_t service(struct CupidGBA* gba, unsigned number, const uint32_t arguments[3],
                        bool thumb, enum CupidPrivMode mode) {
    const uint32_t wide[] = {0xEF000000u | number << 16, 0xEAFFFFFE};
    const uint16_t narrow[] = {(uint16_t)(0xDF00 | number), 0xE7FE};
    memcpy(gba->memory.iwram + 0x100, thumb ? (const void*)narrow : (const void*)wide,
           thumb ? sizeof(narrow) : sizeof(wide));
    cupid_arm_set_priv_mode(&gba->cpu, mode);
    for (unsigned reg = 0; reg < 15; reg++) gba->cpu.gprs[reg] = (int32_t)(0x11220000u + reg);
    for (unsigned reg = 0; reg < 3; reg++) gba->cpu.gprs[reg] = (int32_t)arguments[reg];
    gba->cpu.gprs[ARM_SP] = 0x03007000;
    const uint32_t status = 0xA0000000u | (unmaskedCaller ? 0 : 0x80u) | mode | (thumb ? 32u : 0u);
    gba->cpu.cpsr.packed = (int32_t)status;
    gba->cpu.spsr.packed = 0x6500003F;
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = 0x03000100;
    if (thumb) CupidThumbWritePC(&gba->cpu); else CupidARMWritePC(&gba->cpu);
    const uint64_t start = cupid_timing_global_time(&gba->timing);
    unsigned steps;
    for (steps = 0; steps < 200000; steps++) {
        cupid_gba_step(gba);
        if (gba->cpu.executionMode == (thumb ? MODE_THUMB : MODE_ARM) &&
            (uint32_t)gba->cpu.gprs[ARM_PC] == 0x03000100u + (thumb ? 4u : 8u)) break;
    }
    if (steps == 200000) fprintf(stderr, "Music SWI %02x did not return: clock=%" PRIu64
        " pc=%08" PRIx32 "\n", number, cupid_timing_global_time(&gba->timing), (uint32_t)gba->cpu.gprs[ARM_PC]);
    CHECK(steps < 200000);
    CHECK(gba->cpu.privilegeMode == mode && (uint32_t)gba->cpu.cpsr.packed == status);
    CHECK((uint32_t)gba->cpu.gprs[2] == arguments[2]);
    for (unsigned reg = 4; reg < 15; reg++)
        CHECK((uint32_t)gba->cpu.gprs[reg] == (reg == ARM_SP ? 0x03007000u : 0x11220000u + reg));
    if (mode == MODE_IRQ) CHECK((uint32_t)gba->cpu.spsr.packed == 0x6500003F);
    ++observations;
    return cupid_timing_global_time(&gba->timing) - start;
}
static uint64_t invoke(struct CupidGBA* gba, unsigned number, uint32_t a, uint32_t b, uint32_t c,
                       bool thumb, enum CupidPrivMode mode) {
    const uint32_t arguments[] = {a, b, c};
    return service(gba, number, arguments, thumb, mode);
}
static uint64_t entry_call(struct CupidGBA* gba, uint32_t entry, const uint32_t arguments[3], bool thumb) {
    CHECK((entry & 1) && entry < GBA_SIZE_BIOS);
    const uint32_t wide[] = {0xE59FC008, 0xE1A0E00F, 0xE12FFF1C, 0xEAFFFFFE, entry};
    /* Keep argument R2 while constructing a Thumb return address in LR. */
    uint16_t narrow[] = {0xB404, 0x4B04, 0xA202, 0x3201, 0x4696, 0xBC04, 0x4718, 0,
                         0xE7FE, 0, (uint16_t)entry, (uint16_t)(entry >> 16)};
    memcpy(gba->memory.iwram + 0x100, thumb ? (const void*)narrow : (const void*)wide,
           thumb ? sizeof(narrow) : sizeof(wide));
    cupid_arm_set_priv_mode(&gba->cpu, MODE_SYS);
    for (unsigned reg = 0; reg < 15; reg++) gba->cpu.gprs[reg] = (int32_t)(0x11220000u + reg);
    for (unsigned reg = 0; reg < 3; reg++) gba->cpu.gprs[reg] = (int32_t)arguments[reg];
    gba->cpu.gprs[ARM_SP] = 0x03007000;
    gba->cpu.cpsr.packed = 0xA000009Fu | (thumb ? 32u : 0u);
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = 0x03000100;
    if (thumb) CupidThumbWritePC(&gba->cpu); else CupidARMWritePC(&gba->cpu);
    const uint64_t start = cupid_timing_global_time(&gba->timing);
    unsigned steps;
    for (steps = 0; steps < 200000; steps++) {
        cupid_gba_step(gba);
        if (gba->cpu.executionMode == (thumb ? MODE_THUMB : MODE_ARM) &&
            (uint32_t)gba->cpu.gprs[ARM_PC] == (thumb ? 0x03000112u : 0x03000110u)) break;
    }
    if (steps == 200000) fprintf(stderr, "Music entry %08" PRIx32 " did not return: pc=%08" PRIx32 "\n",
                                 entry, (uint32_t)gba->cpu.gprs[ARM_PC]);
    CHECK(steps < 200000);
    CHECK(gba->cpu.privilegeMode == MODE_SYS);
    CHECK(((uint32_t)gba->cpu.cpsr.packed & 255) == (0x9Fu | (thumb ? 32u : 0u)));
    for (unsigned reg = 4; reg < 12; reg++) CHECK((uint32_t)gba->cpu.gprs[reg] == 0x11220000u + reg);
    CHECK((uint32_t)gba->cpu.gprs[ARM_SP] == 0x03007000);
    ++observations;
    return cupid_timing_global_time(&gba->timing) - start;
}
static struct CupidGBA* machine(uint64_t clock, bool internal) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba)); CHECK(gba);
    cupid_gba_create(gba);
    if (externalBios) CHECK(cupid_load_bios(gba, externalBios));
    cupid_gba_reset(gba);
    cupid_test_bios_caller(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
    cupid_video_reset(&gba->video);
    gba->memory.io[REG_VCOUNT >> 1] = 0;
    cupid_audio_reset(&gba->audio);
    gba->audio.lastSample = gba->audio.sampleOrigin = clock;
    gba->audio.nextSample = clock + 512;
    soundAddress = internal ? 0x03001000 : 0x02001000;
    playerAddress = internal ? 0x03003000 : 0x02003000;
    trackAddress = internal ? 0x03004000 : 0x02006000;
    memset(ram(gba, soundAddress), 0xA5, SOUND_BYTES);
    cupid_io_write(gba, REG_SOUNDBIAS, 0x0200);
    invoke(gba, 0x1A, soundAddress, 0x11220001, 0x11220002, false, MODE_SYS);
    cupid_io_write(gba, REG_TM0CNT_H, 0);
    invoke(gba, 0x1D, 0, 0x11220001, 0x11220002, true, MODE_SYS);
    memset(ram(gba, playerAddress), 0xA5, 0x80);
    memset(ram(gba, trackAddress), 0xA5, TRACK_BYTES);
    /* Opening retains payloads. A voice walk needs caller-owned, valid links. */
    for (unsigned track = 0; track < 16; track++) put(ram(gba, trackAddress), 0x20 + track * 80, 0);
    return gba;
}
static void destroy(struct CupidGBA* gba) { cupid_gba_destroy(gba); free(gba); }

static void opening_preserves_payloads_and_bounds(void) {
    const uint32_t counts[] = {0x80000000, UINT32_MAX, 0, 1, 2, 3, 4, 8, 15, 16, 17, 255, 0x7FFFFFFF};
    for (unsigned region = 0; region < 2; region++) for (unsigned item = 0; item < sizeof(counts)/sizeof(*counts); item++) {
        struct CupidGBA* gba = machine(0, region != 0);
        uint8_t expectedPlayer[0x80], expectedTracks[TRACK_BYTES], expectedSound[SOUND_BYTES];
        memcpy(expectedPlayer, ram(gba, playerAddress), sizeof(expectedPlayer));
        memcpy(expectedTracks, ram(gba, trackAddress), sizeof(expectedTracks));
        memcpy(expectedSound, ram(gba, soundAddress), sizeof(expectedSound));
        invoke(gba, 0x20, playerAddress, trackAddress, counts[item], (item & 1) != 0, item & 2 ? MODE_IRQ : MODE_SYS);
        if ((int32_t)counts[item] > 0) {
            const unsigned accepted = counts[item] > 16 ? 16 : counts[item];
            memset(expectedPlayer, 0, 64);
            put(expectedPlayer, 4, 0x80000000);
            expectedPlayer[8] = (uint8_t)accepted;
            put(expectedPlayer, 44, trackAddress);
            put(expectedPlayer, 52, MUSIC_READY);
            for (unsigned track = 0; track < accepted; track++) expectedTracks[track * 80] = 0;
            const uint32_t callback = word(ram(gba, soundAddress), 32);
            CHECK((callback & 1) && callback < GBA_SIZE_BIOS);
            CHECK((uint32_t)gba->cpu.gprs[0] == callback && (uint32_t)gba->cpu.gprs[1] == soundAddress);
            put(expectedSound, 32, callback);
            put(expectedSound, 36, playerAddress);
        }
        CHECK(!memcmp(expectedPlayer, ram(gba, playerAddress), sizeof(expectedPlayer)));
        CHECK(!memcmp(expectedTracks, ram(gba, trackAddress), sizeof(expectedTracks)));
        CHECK(!memcmp(expectedSound, ram(gba, soundAddress), sizeof(expectedSound)));
        destroy(gba);
    }
}

static void opening_with_parent_guards(void) {
    const uint32_t guards[] = {0, MUSIC_READY - 1, MUSIC_READY + 1, UINT32_MAX};
    for (unsigned region = 0; region < 2; region++) for (unsigned item = 0; item < 4; item++) {
        struct CupidGBA* gba = machine(UINT32_MAX - 200000u, region != 0);
        put(ram(gba, soundAddress), 0, guards[item]);
        uint8_t expectedPlayer[0x80], expectedTracks[TRACK_BYTES], expectedSound[SOUND_BYTES];
        memcpy(expectedPlayer, ram(gba, playerAddress), sizeof(expectedPlayer));
        memcpy(expectedTracks, ram(gba, trackAddress), sizeof(expectedTracks));
        memcpy(expectedSound, ram(gba, soundAddress), sizeof(expectedSound));
        invoke(gba, 0x20, playerAddress, trackAddress, 2, (item & 1) != 0, MODE_SYS);
        memset(expectedPlayer, 0, 64);
        put(expectedPlayer, 4, 0x80000000);
        expectedPlayer[8] = 2;
        put(expectedPlayer, 44, trackAddress);
        expectedTracks[0] = expectedTracks[80] = 0;
        CHECK(!memcmp(expectedPlayer, ram(gba, playerAddress), sizeof(expectedPlayer)));
        CHECK(!memcmp(expectedTracks, ram(gba, trackAddress), sizeof(expectedTracks)));
        CHECK(!memcmp(expectedSound, ram(gba, soundAddress), sizeof(expectedSound)));
        destroy(gba);
    }
}

static void discovery_writes_exactly_thirty_six_entries(void) {
    const unsigned aliases[] = {5, 6, 7, 8, 21, 22, 24, 25, 26, 28};
    for (unsigned region = 0; region < 2; region++) for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine(0, region != 0);
        const uint32_t destination = region ? 0x03005000 : 0x02012000;
        uint8_t* result = ram(gba, destination);
        memset(result, 0xA5, 0x140);
        const uint64_t clocks = invoke(gba, 0x2A, destination, 0x11220001, 0x11220002, thumb != 0, MODE_SYS);
        if (!region) CHECK(clocks == 1422);
        CHECK((uint32_t)gba->cpu.gprs[0] == destination + 144 && gba->cpu.gprs[1] == 0);
        const uint32_t table = word(ram(gba, soundAddress), 52);
        CHECK(table + 144 <= GBA_SIZE_BIOS && !(table & 3));
        CHECK(!memcmp(result, gba->memory.bios + table, 144));
        for (unsigned entry = 0; entry < 36; entry++) CHECK((word(result, entry * 4) & 1) && word(result, entry * 4) < GBA_SIZE_BIOS);
        for (unsigned entry = 0; entry < sizeof(aliases)/sizeof(*aliases); entry++) CHECK(word(result, aliases[entry] * 4) == word(result, 0));
        for (unsigned byte = 144; byte < 0x140; byte++) CHECK(result[byte] == 0xA5);
        destroy(gba);
    }
}

static void open_player(struct CupidGBA* gba, unsigned count) {
    invoke(gba, 0x20, playerAddress, trackAddress, count, false, MODE_SYS);
    CHECK(word(ram(gba, playerAddress), 52) == MUSIC_READY);
}
static void read_entries(struct CupidGBA* gba, uint32_t entries[36]) {
    memset(gba->memory.ewram + 0x12000, 0xA5, 144);
    invoke(gba, 0x2A, 0x02012000, 0x11220001, 0x11220002, false, MODE_SYS);
    memcpy(entries, gba->memory.ewram + 0x12000, 144);
    for (unsigned item = 0; item < 36; item++) CHECK((entries[item] & 1) && entries[item] < GBA_SIZE_BIOS);
}

static void exported_clear_has_complete_writes_and_guards(void) {
    for (unsigned region = 0; region < 2; region++) for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine(0, region != 0);
        uint32_t entries[36]; read_entries(gba, entries);
        const uint32_t address = region ? 0x03006000 : 0x02014000;
        uint8_t* destination = ram(gba, address); memset(destination, 0xA5, 96);
        uint8_t expectedSound[SOUND_BYTES]; memcpy(expectedSound, ram(gba, soundAddress), sizeof(expectedSound));
        const uint32_t arguments[] = {address, 0x11220001, 0x11220002};
        entry_call(gba, entries[35], arguments, thumb != 0);
        for (unsigned byte = 0; byte < 96; byte++) CHECK(destination[byte] == (byte < 64 ? 0 : 0xA5));
        CHECK((uint32_t)gba->cpu.gprs[0] == address + 64 && !gba->cpu.gprs[1] && !gba->cpu.gprs[2] && !gba->cpu.gprs[3]);
        CHECK(!memcmp(expectedSound, ram(gba, soundAddress), sizeof(expectedSound)));
        destroy(gba);
    }
}

static void exported_unlink_preserves_other_members(void) {
    for (unsigned variant = 0; variant < 6; variant++) for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine(0, false);
        open_player(gba, 2);
        uint32_t entries[36]; read_entries(gba, entries);
        uint8_t* sound = ram(gba, soundAddress);
        uint8_t* track = ram(gba, trackAddress);
        const unsigned removed = variant % 3;
        for (unsigned voice = 0; voice < 3; voice++) {
            uint8_t* state = sound + 0x50 + voice * 64;
            memset(state, 0xA5, 64);
            put(state, 44, variant == 5 && voice == removed ? 0 : trackAddress);
            put(state, 48, voice ? soundAddress + 0x50 + (voice - 1) * 64 : 0);
            put(state, 52, voice == 2 ? 0 : soundAddress + 0x50 + (voice + 1) * 64);
        }
        put(track, 32, soundAddress + 0x50);
        uint8_t expectedSound[SOUND_BYTES], expectedTracks[TRACK_BYTES];
        memcpy(expectedSound, sound, sizeof(expectedSound)); memcpy(expectedTracks, track, sizeof(expectedTracks));
        const uint32_t arguments[] = {soundAddress + 0x50 + removed * 64, 0x11220001, 0x11220002};
        entry_call(gba, entries[34], arguments, thumb != 0);
        if (variant != 5) {
            const uint32_t previous = removed ? soundAddress + 0x50 + (removed - 1) * 64 : 0;
            const uint32_t next = removed == 2 ? 0 : soundAddress + 0x50 + (removed + 1) * 64;
            if (removed) put(expectedSound, 0x50 + (removed - 1) * 64 + 52, next);
            else put(expectedTracks, 32, next);
            if (next) put(expectedSound, 0x50 + (removed + 1) * 64 + 48, previous);
            put(expectedSound, 0x50 + removed * 64 + 44, 0);
        }
        CHECK(!memcmp(expectedSound, sound, sizeof(expectedSound)));
        CHECK(!memcmp(expectedTracks, track, sizeof(expectedTracks)));
        destroy(gba);
    }
}

static void controller_batch(unsigned batch) {
    const unsigned selectors[] = {9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 23};
    const unsigned values[] = {0, 64, 127, 255};
    for (unsigned item = batch * 3; item < (batch + 1) * 3 && item < sizeof(selectors)/sizeof(*selectors); item++)
      for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine(0, false);
        open_player(gba, 2);
        uint32_t entries[36]; read_entries(gba, entries);
        uint8_t* player = ram(gba, playerAddress);
        uint8_t* tracks = ram(gba, trackAddress);
        put(player, 48, 0x0200C000); put_half(player, 30, 384);
        tracks[0] = 0x90; tracks[22] = 0x73; tracks[24] = 2;
        for (unsigned tone = 0; tone < 256; tone++) for (unsigned byte = 0; byte < 12; byte++)
            gba->memory.ewram[0xC000 + tone * 12 + byte] = (uint8_t)(tone * 17 + byte * 39);
        uint8_t initialPlayer[0x80], initialTracks[TRACK_BYTES], initialSound[SOUND_BYTES];
        memcpy(initialPlayer, player, sizeof(initialPlayer)); memcpy(initialTracks, tracks, sizeof(initialTracks));
        memcpy(initialSound, ram(gba, soundAddress), sizeof(initialSound));
        for (unsigned value = 0; value < 4; value++) {
            const unsigned selector = selectors[item];
            const unsigned operand = selector == 20 ? (value < 3 ? value : 255) : values[value];
            memcpy(player, initialPlayer, sizeof(initialPlayer)); memcpy(tracks, initialTracks, sizeof(initialTracks));
            uint8_t expectedPlayer[0x80], expectedTracks[TRACK_BYTES];
            memcpy(expectedPlayer, initialPlayer, sizeof(expectedPlayer)); memcpy(expectedTracks, initialTracks, sizeof(expectedTracks));
            gba->memory.ewram[0x9000] = (uint8_t)operand;
            put(tracks, 64, 0x02009000); put(expectedTracks, 64, 0x02009001);
            unsigned flags = 0;
            switch (selector) {
            case 9: expectedTracks[29] = (uint8_t)operand; break;
            case 10: put_half(expectedPlayer, 28, (uint16_t)(operand * 2));
                     put_half(expectedPlayer, 32, (uint16_t)(operand * 2 * 384 / 256)); break;
            case 11: expectedTracks[10] = (uint8_t)operand; flags = 12; break;
            case 12: memcpy(expectedTracks + 36, gba->memory.ewram + 0xC000 + operand * 12, 12); break;
            case 13: expectedTracks[18] = (uint8_t)operand; flags = 3; break;
            case 14: expectedTracks[20] = (uint8_t)(operand - 64); flags = 3; break;
            case 15: expectedTracks[14] = (uint8_t)(operand - 64); flags = 12; break;
            case 16: expectedTracks[15] = (uint8_t)operand; flags = 12; break;
            case 17: expectedTracks[25] = (uint8_t)operand; if (!operand) expectedTracks[22] = 0; break;
            case 18: expectedTracks[27] = (uint8_t)operand; break;
            case 19: expectedTracks[23] = (uint8_t)operand; if (!operand) expectedTracks[22] = 0; break;
            case 20: if (operand != 2) { expectedTracks[24] = (uint8_t)operand; flags = 15; } break;
            case 23: expectedTracks[12] = (uint8_t)(operand - 64); flags = 12; break;
            default: CHECK(false);
            }
            expectedTracks[0] |= (uint8_t)flags;
            const uint32_t arguments[] = {playerAddress, trackAddress, 0x11220002};
            entry_call(gba, entries[selector], arguments, thumb != 0);
            CHECK(!memcmp(expectedPlayer, player, sizeof(expectedPlayer)));
            CHECK(!memcmp(expectedTracks, tracks, sizeof(expectedTracks)));
            CHECK(!memcmp(initialSound, ram(gba, soundAddress), sizeof(initialSound)));
        }
        destroy(gba);
      }
}

static void fine_aliases_release_and_unlink_all_voices(void) {
    const unsigned selectors[] = {0, 5, 6, 7, 8, 21, 22, 24, 25, 26, 28};
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine(0, false);
        open_player(gba, 2);
        uint32_t entries[36]; read_entries(gba, entries);
        uint8_t initialPlayer[0x80], initialTracks[TRACK_BYTES], initialSound[SOUND_BYTES];
        memcpy(initialPlayer, ram(gba, playerAddress), sizeof(initialPlayer));
        memcpy(initialTracks, ram(gba, trackAddress), sizeof(initialTracks));
        memcpy(initialSound, ram(gba, soundAddress), sizeof(initialSound));
        for (unsigned item = 0; item < sizeof(selectors)/sizeof(*selectors); item++) {
            uint8_t* player = ram(gba, playerAddress);
            uint8_t* tracks = ram(gba, trackAddress);
            uint8_t* sound = ram(gba, soundAddress);
            memcpy(player, initialPlayer, sizeof(initialPlayer)); memcpy(tracks, initialTracks, sizeof(initialTracks));
            memcpy(sound, initialSound, sizeof(initialSound));
            tracks[0] = 0x85; put(tracks, 32, soundAddress + 0x50);
            const uint8_t statuses[] = {0x12, 4, 0};
            for (unsigned voice = 0; voice < 3; voice++) {
                uint8_t* state = sound + 0x50 + voice * 64;
                state[0] = statuses[voice]; put(state, 44, trackAddress);
                put(state, 48, voice ? soundAddress + 0x50 + (voice - 1) * 64 : 0);
                put(state, 52, voice == 2 ? 0 : soundAddress + 0x50 + (voice + 1) * 64);
            }
            uint8_t expectedTracks[TRACK_BYTES], expectedSound[SOUND_BYTES];
            memcpy(expectedTracks, tracks, sizeof(expectedTracks)); memcpy(expectedSound, sound, sizeof(expectedSound));
            expectedTracks[0] = 0; put(expectedTracks, 32, 0);
            for (unsigned voice = 0; voice < 3; voice++) {
                uint8_t* state = expectedSound + 0x50 + voice * 64;
                if (voice < 2) state[0] |= 64;
                put(state, 44, 0); put(state, 48, 0);
            }
            const uint32_t arguments[] = {playerAddress, trackAddress, 0x11220002};
            entry_call(gba, entries[selectors[item]], arguments, thumb != 0);
            CHECK(!memcmp(initialPlayer, player, sizeof(initialPlayer)));
            CHECK(!memcmp(expectedTracks, tracks, sizeof(expectedTracks)));
            CHECK(!memcmp(expectedSound, sound, sizeof(expectedSound)));
        }
        destroy(gba);
    }
}

static void flow_entries_preserve_pattern_and_repeat_bounds(void) {
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine(0, false); open_player(gba, 2);
        uint32_t entries[36]; read_entries(gba, entries);
        uint8_t initialPlayer[0x80], initialTracks[TRACK_BYTES], initialSound[SOUND_BYTES];
        memcpy(initialPlayer, ram(gba, playerAddress), sizeof(initialPlayer));
        memcpy(initialTracks, ram(gba, trackAddress), sizeof(initialTracks));
        memcpy(initialSound, ram(gba, soundAddress), sizeof(initialSound));
        const uint32_t arguments[] = {playerAddress, trackAddress, 0x11220002};
        for (unsigned selector = 1; selector <= 4; selector++) for (unsigned variant = 0; variant < 4; variant++) {
            uint8_t* player = ram(gba, playerAddress); uint8_t* tracks = ram(gba, trackAddress);
            memcpy(player, initialPlayer, sizeof(initialPlayer)); memcpy(tracks, initialTracks, sizeof(initialTracks));
            tracks[0] = 0x80; tracks[2] = (uint8_t)variant; tracks[3] = (uint8_t)(variant ? variant - 1 : 255);
            put(tracks, 64, 0x02009001);
            for (unsigned slot = 0; slot < 3; slot++) put(tracks, 68 + slot * 4, 0x0200A003 + slot * 0x101);
            memset(gba->memory.ewram + 0x9000, 0xA5, 16);
            const unsigned limit = variant == 0 ? 0 : (variant == 1 ? 1 : (variant == 2 ? 4 : 255));
            put(gba->memory.ewram, 0x9001, 0x02009403);
            if (selector == 4) { gba->memory.ewram[0x9001] = (uint8_t)limit; put(gba->memory.ewram, 0x9002, 0x02009403); }
            uint8_t expectedTracks[TRACK_BYTES]; memcpy(expectedTracks, tracks, sizeof(expectedTracks));
            if (selector == 1) put(expectedTracks, 64, 0x02009403);
            if (selector == 2) {
                if (variant == 3) expectedTracks[0] = 0;
                else { expectedTracks[2]++; put(expectedTracks, 68 + variant * 4, 0x02009005); put(expectedTracks, 64, 0x02009403); }
            }
            if (selector == 3 && variant) {
                expectedTracks[2]--; put(expectedTracks, 64, 0x0200A003 + (variant - 1) * 0x101);
            }
            if (selector == 4) {
                if (!limit) put(expectedTracks, 64, 0x02009403);
                else if ((unsigned)tracks[3] + 1 >= limit) { expectedTracks[3] = 0; put(expectedTracks, 64, 0x02009006); }
                else { expectedTracks[3]++; put(expectedTracks, 64, 0x02009403); }
            }
            entry_call(gba, entries[selector], arguments, thumb != 0);
            CHECK(!memcmp(initialPlayer, player, sizeof(initialPlayer)));
            CHECK(!memcmp(expectedTracks, tracks, sizeof(expectedTracks)));
            CHECK(!memcmp(initialSound, ram(gba, soundAddress), sizeof(initialSound)));
        }
        destroy(gba);
    }
}

static void port_entry_writes_byte_and_advances_two_operands(void) {
    const unsigned values[] = {0, 0x12, 0x77, 0xFF};
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine(0, false); open_player(gba, 2);
        uint32_t entries[36]; read_entries(gba, entries);
        for (unsigned item = 0; item < 4; item++) {
            uint8_t expectedTracks[TRACK_BYTES], expectedSound[SOUND_BYTES], expectedPlayer[0x80];
            put(ram(gba, trackAddress), 64, 0x02009000);
            memcpy(expectedTracks, ram(gba, trackAddress), sizeof(expectedTracks));
            memcpy(expectedSound, ram(gba, soundAddress), sizeof(expectedSound));
            memcpy(expectedPlayer, ram(gba, playerAddress), sizeof(expectedPlayer));
            put(expectedTracks, 64, 0x02009002);
            gba->memory.ewram[0x9000] = 0x20; gba->memory.ewram[0x9001] = (uint8_t)values[item];
            const uint32_t arguments[] = {playerAddress, trackAddress, 0x11220002};
            entry_call(gba, entries[27], arguments, thumb != 0);
            CHECK(!memcmp(expectedTracks, ram(gba, trackAddress), sizeof(expectedTracks)));
            CHECK(!memcmp(expectedPlayer, ram(gba, playerAddress), sizeof(expectedPlayer)));
            CHECK(!memcmp(expectedSound, ram(gba, soundAddress), sizeof(expectedSound)));
            CHECK((gba->memory.io[REG_SOUNDCNT_L >> 1] & 255) == (values[item] & 0x77));
            CHECK(gba->audio.psg.volumeRight == (values[item] & 7) && gba->audio.psg.volumeLeft == (values[item] >> 4 & 7));
        }
        destroy(gba);
    }
}

static void playback_entry_batch(unsigned batch) {
    const unsigned samples[] = {96, 132, 176, 224, 264, 304, 352, 448, 528, 608, 672, 704};
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine(0, false);
        uint32_t entries[36]; read_entries(gba, entries);
        put(ram(gba, soundAddress), 0, MUSIC_READY + 1);
        for (unsigned rate = batch * 4 + 1; rate <= (batch + 1) * 4; rate++) {
            uint8_t expectedSound[SOUND_BYTES]; memcpy(expectedSound, ram(gba, soundAddress), sizeof(expectedSound));
            const uint32_t nominal = (samples[rate - 1] * 597275u + 5000u) / 10000u;
            expectedSound[8] = (uint8_t)rate; expectedSound[11] = (uint8_t)(1584 / samples[rate - 1]);
            put(expectedSound, 16, samples[rate - 1]); put(expectedSound, 20, nominal);
            put(expectedSound, 24, ((0x1000000u / nominal) + 1) >> 1);
            const uint32_t arguments[] = {rate << 16, 0x11220001, 0x11220002};
            entry_call(gba, entries[30], arguments, thumb != 0);
            CHECK(!memcmp(expectedSound, ram(gba, soundAddress), sizeof(expectedSound)));
            CHECK(gba->timers[0].reload == (uint16_t)-(int)(280896 / samples[rate - 1]) && gba->timers[0].control == 0x80);
            CHECK(gba->memory.dma[1].reg == 0xB600 && gba->memory.dma[2].reg == 0xB600);
            CHECK(gba->video.vcount == 159);
            cupid_io_write(gba, REG_TM0CNT_H, 0);
        }
        destroy(gba);
    }
}

static void track_stop_entry_retains_player_and_track_status(void) {
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine(0, false); open_player(gba, 2);
        uint32_t entries[36]; read_entries(gba, entries);
        const uint32_t arguments[] = {playerAddress, trackAddress, 0x11220002};
        for (unsigned active = 0; active < 2; active++) {
            uint8_t* tracks = ram(gba, trackAddress); uint8_t* sound = ram(gba, soundAddress);
            tracks[0] = active ? 0x85 : 1; put(tracks, 32, soundAddress + 0x50);
            put(ram(gba, playerAddress), 52, MUSIC_READY + 1);
            const uint8_t statuses[] = {0x12, 0, 0x40};
            for (unsigned voice = 0; voice < 3; voice++) {
                uint8_t* channel = sound + 0x50 + voice * 64;
                channel[0] = statuses[voice]; channel[1] = (uint8_t)voice;
                put(channel, 44, trackAddress);
                put(channel, 52, voice == 2 ? 0 : soundAddress + 0x50 + (voice + 1) * 64);
            }
            uint8_t expectedSound[SOUND_BYTES], expectedTracks[TRACK_BYTES], expectedPlayer[0x80];
            memcpy(expectedSound, sound, sizeof(expectedSound)); memcpy(expectedTracks, tracks, sizeof(expectedTracks));
            memcpy(expectedPlayer, ram(gba, playerAddress), sizeof(expectedPlayer));
            if (active) {
                put(expectedTracks, 32, 0);
                for (unsigned voice = 0; voice < 3; voice++) if (statuses[voice]) {
                    expectedSound[0x50 + voice * 64] = 0; put(expectedSound, 0x50 + voice * 64 + 44, 0);
                }
            }
            entry_call(gba, entries[31], arguments, thumb != 0);
            CHECK(!memcmp(expectedSound, sound, sizeof(expectedSound)));
            CHECK(!memcmp(expectedTracks, tracks, sizeof(expectedTracks)));
            CHECK(!memcmp(expectedPlayer, ram(gba, playerAddress), sizeof(expectedPlayer)));
        }
        destroy(gba);
    }
}

static void fade_entry_keeps_halfword_interval_and_wrapping_level(void) {
    const uint16_t intervals[] = {0, 2, 2, 2, 2, 2};
    const uint16_t counters[] = {3, 2, 1, 1, 1, 0};
    const uint16_t levels[] = {256, 256, 256, 16, 0, 256};
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine(0, false); open_player(gba, 4);
        uint32_t entries[36]; read_entries(gba, entries);
        uint8_t initialPlayer[0x80], initialTracks[TRACK_BYTES], initialSound[SOUND_BYTES];
        memcpy(initialPlayer, ram(gba, playerAddress), sizeof(initialPlayer));
        memcpy(initialTracks, ram(gba, trackAddress), sizeof(initialTracks));
        memcpy(initialSound, ram(gba, soundAddress), sizeof(initialSound));
        for (unsigned item = 0; item < 6; item++) {
            uint8_t* player = ram(gba, playerAddress); uint8_t* tracks = ram(gba, trackAddress);
            memcpy(player, initialPlayer, sizeof(initialPlayer)); memcpy(tracks, initialTracks, sizeof(initialTracks));
            put(player, 52, MUSIC_READY + 1);
            put_half(player, 36, intervals[item]); put_half(player, 38, counters[item]); put_half(player, 40, levels[item]);
            for (unsigned track = 0; track < 4; track++) tracks[track * 80] = track & 1 ? 1 : 0x80;
            uint8_t expectedPlayer[0x80], expectedTracks[TRACK_BYTES];
            memcpy(expectedPlayer, player, sizeof(expectedPlayer)); memcpy(expectedTracks, tracks, sizeof(expectedTracks));
            if (intervals[item]) {
                const uint16_t counter = (uint16_t)(counters[item] - 1);
                put_half(expectedPlayer, 38, counter);
                if (!counter) {
                    const uint16_t level = (uint16_t)(levels[item] - 16);
                    put_half(expectedPlayer, 40, level);
                    if ((int16_t)level <= 0) {
                        for (unsigned track = 0; track < 4; track++) expectedTracks[track * 80] = 0;
                    } else {
                        put_half(expectedPlayer, 38, intervals[item]);
                        for (unsigned track = 0; track < 4; track++) if (!(track & 1)) {
                            expectedTracks[track * 80 + 19] = (uint8_t)(level >> 2);
                            expectedTracks[track * 80] |= 3;
                        }
                    }
                }
            }
            const uint32_t arguments[] = {playerAddress, 0x11220001, 0x11220002};
            entry_call(gba, entries[32], arguments, thumb != 0);
            CHECK(!memcmp(expectedPlayer, player, sizeof(expectedPlayer)));
            CHECK(!memcmp(expectedTracks, tracks, sizeof(expectedTracks)));
            CHECK(!memcmp(initialSound, ram(gba, soundAddress), sizeof(initialSound)));
        }
        destroy(gba);
    }
}

static void tie_entry_releases_only_the_first_matching_live_note(void) {
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine(0, false); open_player(gba, 2);
        uint32_t entries[36]; read_entries(gba, entries);
        for (unsigned previous = 0; previous < 2; previous++) {
            uint8_t* tracks = ram(gba, trackAddress); uint8_t* sound = ram(gba, soundAddress);
            tracks[5] = 61; put(tracks, 64, 0x02009000); put(tracks, 32, soundAddress + 0x50);
            gba->memory.ewram[0x9000] = previous ? 0x84 : 60;
            const uint8_t states[] = {4, 0x12, 0x80};
            for (unsigned voice = 0; voice < 3; voice++) {
                uint8_t* channel = sound + 0x50 + voice * 64;
                channel[0] = states[voice]; channel[17] = previous ? 61 : 60;
                put(channel, 52, voice == 2 ? 0 : soundAddress + 0x50 + (voice + 1) * 64);
            }
            uint8_t expectedTracks[TRACK_BYTES], expectedSound[SOUND_BYTES], expectedPlayer[0x80];
            memcpy(expectedTracks, tracks, sizeof(expectedTracks)); memcpy(expectedSound, sound, sizeof(expectedSound));
            memcpy(expectedPlayer, ram(gba, playerAddress), sizeof(expectedPlayer));
            if (!previous) { expectedTracks[5] = 60; put(expectedTracks, 64, 0x02009001); }
            expectedSound[0x90] |= 64;
            const uint32_t arguments[] = {playerAddress, trackAddress, 0x11220002};
            entry_call(gba, entries[29], arguments, thumb != 0);
            CHECK(!memcmp(expectedTracks, tracks, sizeof(expectedTracks)));
            CHECK(!memcmp(expectedSound, sound, sizeof(expectedSound)));
            CHECK(!memcmp(expectedPlayer, ram(gba, playerAddress), sizeof(expectedPlayer)));
        }
        destroy(gba);
    }
}

static void source_guard_keeps_bios_data_boundary(void) {
    const uint32_t sources[] = {0, 0x3737, 0x3738, 0x3FFF, 0x4000, 0x10000, 0x01FFFFFF, 0x02000000, 0x03000000};
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine(0, false); open_player(gba, 2);
        uint32_t entries[36]; read_entries(gba, entries);
        gba->memory.ewram[0] = 0x56; gba->memory.iwram[0] = 0x67;
        for (unsigned item = 0; item < sizeof(sources)/sizeof(*sources); item++) {
            uint8_t* tracks = ram(gba, trackAddress);
            put(tracks, 64, sources[item]);
            uint8_t expectedTracks[TRACK_BYTES], expectedSound[SOUND_BYTES], expectedPlayer[0x80];
            memcpy(expectedTracks, tracks, sizeof(expectedTracks));
            memcpy(expectedSound, ram(gba, soundAddress), sizeof(expectedSound));
            memcpy(expectedPlayer, ram(gba, playerAddress), sizeof(expectedPlayer));
            put(expectedTracks, 64, sources[item] + 1);
            const bool biosData = sources[item] >= 0x3738 && sources[item] < GBA_SIZE_BIOS;
            expectedTracks[29] = biosData ? gba->memory.bios[sources[item]] :
                (sources[item] == 0x02000000 ? 0x56 : (sources[item] == 0x03000000 ? 0x67 : 0));
            const uint32_t arguments[] = {playerAddress, trackAddress, 0x11220002};
            entry_call(gba, entries[9], arguments, thumb != 0);
            CHECK(!memcmp(expectedTracks, tracks, sizeof(expectedTracks)));
            CHECK(!memcmp(expectedSound, ram(gba, soundAddress), sizeof(expectedSound)));
            CHECK(!memcmp(expectedPlayer, ram(gba, playerAddress), sizeof(expectedPlayer)));
        }
        destroy(gba);
    }
}

static void starting_clips_tracks_and_retains_unrelated_fields(void) {
    const unsigned songCounts[] = {0, 1, 2, 3, 16, 17, 255};
    const unsigned modes[] = {0, 1, 0x80, 0x85, 0xFF};
    for (unsigned region = 0; region < 2; region++) for (unsigned item = 0; item < 7; item++) {
        struct CupidGBA* gba = machine(0, region != 0);
        const unsigned capacity = item < 4 ? 2 : 16;
        open_player(gba, capacity);
        uint8_t* player = ram(gba, playerAddress);
        uint8_t* tracks = ram(gba, trackAddress);
        uint8_t* song = gba->memory.ewram + 0x8000;
        memset(song, 0, 0x100);
        song[0] = (uint8_t)songCounts[item]; song[2] = (uint8_t)(item * 37);
        song[3] = (uint8_t)modes[item % 5]; put(song, 4, 0x0200C000);
        for (unsigned track = 0; track < 16; track++) put(song, 8 + track * 4, 0x02009000 + track * 0x100);
        put(player, 4, 0xFEDCBA98); put(player, 24, 0x76543210); put(player, 28, 0x34561234);
        put(player, 32, 0x12343210); put(player, 36, 0x77773333); put(player, 40, 0x12345678);
        ram(gba, soundAddress)[5] = 31;
        uint8_t expectedPlayer[0x80], expectedTracks[TRACK_BYTES], expectedSound[SOUND_BYTES];
        memcpy(expectedPlayer, player, sizeof(expectedPlayer));
        memcpy(expectedTracks, tracks, sizeof(expectedTracks));
        memcpy(expectedSound, ram(gba, soundAddress), sizeof(expectedSound));
        invoke(gba, 0x21, playerAddress, 0x02008000, 0x11220002, (item & 1) != 0, item & 2 ? MODE_IRQ : MODE_SYS);
        put(expectedPlayer, 0, 0x02008000); put(expectedPlayer, 4, 0); expectedPlayer[9] = song[2];
        put(expectedPlayer, 28, 0x01000096); put(expectedPlayer, 32, 150);
        expectedPlayer[36] = expectedPlayer[37] = 0; put(expectedPlayer, 48, 0x0200C000);
        for (unsigned track = 0; track < capacity; track++) {
            const bool active = track < songCounts[item];
            expectedTracks[track * 80] = active ? 0xC0 : 0;
            if (active) put(expectedTracks, track * 80 + 64, 0x02009000 + track * 0x100);
        }
        if (song[3] & 0x80) expectedSound[5] = song[3] & 127;
        CHECK(!memcmp(expectedPlayer, player, sizeof(expectedPlayer)));
        CHECK(!memcmp(expectedTracks, tracks, sizeof(expectedTracks)));
        CHECK(!memcmp(expectedSound, ram(gba, soundAddress), sizeof(expectedSound)));
        CHECK(gba->memory.io[REG_SOUNDBIAS >> 1] == 0x4200);
        CHECK((uint32_t)gba->cpu.gprs[0] == MUSIC_READY);
        destroy(gba);
    }
}

static void stopping_preserves_links_and_inactive_owners(void) {
    for (unsigned region = 0; region < 2; region++) for (unsigned variant = 0; variant < 6; variant++) {
        struct CupidGBA* gba = machine(0, region != 0);
        open_player(gba, 4);
        uint8_t* player = ram(gba, playerAddress);
        uint8_t* tracks = ram(gba, trackAddress);
        uint8_t* sound = ram(gba, soundAddress);
        put(player, 4, 0x12345678);
        for (unsigned track = 0; track < 4; track++) {
            uint8_t* state = tracks + track * 80;
            state[0] = track == variant ? 1 : 0x85;
            const uint32_t first = soundAddress + 0x50 + track * 128;
            put(state, 32, first);
            for (unsigned voice = 0; voice < 2; voice++) {
                uint8_t* channel = ram(gba, first + voice * 64);
                channel[0] = (uint8_t)((track + voice + variant) % 3 ? 0x12 : 0);
                channel[1] = (uint8_t)(variant % 5);
                put(channel, 44, trackAddress + track * 80);
                put(channel, 48, voice ? first : 0);
                put(channel, 52, voice ? 0 : first + 64);
            }
        }
        uint8_t expectedPlayer[0x80], expectedTracks[TRACK_BYTES], expectedSound[SOUND_BYTES];
        memcpy(expectedPlayer, player, sizeof(expectedPlayer));
        memcpy(expectedTracks, tracks, sizeof(expectedTracks));
        memcpy(expectedSound, sound, sizeof(expectedSound));
        invoke(gba, 0x22, playerAddress, 0x11220001, 0x11220002, (variant & 1) != 0, variant & 2 ? MODE_IRQ : MODE_SYS);
        put(expectedPlayer, 4, 0x92345678);
        for (unsigned track = 0; track < 4; track++) if (expectedTracks[track * 80] & 0x80) {
            put(expectedTracks, track * 80 + 32, 0);
            for (unsigned voice = 0; voice < 2; voice++) {
                uint8_t* channel = expectedSound + 0x50 + track * 128 + voice * 64;
                if (channel[0]) { channel[0] = 0; put(channel, 44, 0); }
            }
        }
        CHECK(!memcmp(expectedPlayer, player, sizeof(expectedPlayer)));
        CHECK(!memcmp(expectedTracks, tracks, sizeof(expectedTracks)));
        CHECK(!memcmp(expectedSound, sound, sizeof(expectedSound)));
        const uint32_t returned = (uint32_t)gba->cpu.gprs[0];
        CHECK((returned & 1) && returned < GBA_SIZE_BIOS);
        /* R0 retains the internal Thumb return following the final track stop. */
        uint16_t instruction; memcpy(&instruction, gba->memory.bios + (returned & ~1u), 2);
        CHECK(instruction == 0x3450);
        destroy(gba);
    }
}

static void continuation_changes_only_the_pause_bit(void) {
    const uint32_t statuses[] = {0, 1, 0x7FFFFFFF, 0x80000000, 0x81234567, UINT32_MAX};
    for (unsigned region = 0; region < 2; region++) for (unsigned item = 0; item < 6; item++) {
        struct CupidGBA* gba = machine(UINT64_C(0x300000000), region != 0);
        open_player(gba, 2);
        put(ram(gba, playerAddress), 4, statuses[item]);
        uint8_t expectedPlayer[0x80], expectedTracks[TRACK_BYTES], expectedSound[SOUND_BYTES];
        memcpy(expectedPlayer, ram(gba, playerAddress), sizeof(expectedPlayer));
        memcpy(expectedTracks, ram(gba, trackAddress), sizeof(expectedTracks));
        memcpy(expectedSound, ram(gba, soundAddress), sizeof(expectedSound));
        invoke(gba, 0x23, playerAddress, 0x11220001, 0x11220002, (item & 1) != 0, item & 2 ? MODE_IRQ : MODE_SYS);
        put(expectedPlayer, 4, statuses[item] & 0x7FFFFFFF);
        CHECK(!memcmp(expectedPlayer, ram(gba, playerAddress), sizeof(expectedPlayer)));
        CHECK(!memcmp(expectedTracks, ram(gba, trackAddress), sizeof(expectedTracks)));
        CHECK(!memcmp(expectedSound, ram(gba, soundAddress), sizeof(expectedSound)));
        CHECK((uint32_t)gba->cpu.gprs[0] == playerAddress && (uint32_t)gba->cpu.gprs[1] == MUSIC_READY);
        destroy(gba);
    }
}

static void fade_setup_retains_status_and_payload(void) {
    const uint32_t intervals[] = {0, 1, 2, 0xFFFF, 0x10000, UINT32_MAX};
    for (unsigned region = 0; region < 2; region++) for (unsigned item = 0; item < 6; item++) {
        struct CupidGBA* gba = machine(0, region != 0);
        open_player(gba, 2);
        uint8_t expectedPlayer[0x80], expectedTracks[TRACK_BYTES], expectedSound[SOUND_BYTES];
        memcpy(expectedPlayer, ram(gba, playerAddress), sizeof(expectedPlayer));
        memcpy(expectedTracks, ram(gba, trackAddress), sizeof(expectedTracks));
        memcpy(expectedSound, ram(gba, soundAddress), sizeof(expectedSound));
        invoke(gba, 0x24, playerAddress, intervals[item], 0x11220002, (item & 1) != 0, item & 2 ? MODE_IRQ : MODE_SYS);
        put(expectedPlayer, 36, (intervals[item] & 0xFFFF) * 0x10001u);
        expectedPlayer[40] = 0; expectedPlayer[41] = 1;
        CHECK(!memcmp(expectedPlayer, ram(gba, playerAddress), sizeof(expectedPlayer)));
        CHECK(!memcmp(expectedTracks, ram(gba, trackAddress), sizeof(expectedTracks)));
        CHECK(!memcmp(expectedSound, ram(gba, soundAddress), sizeof(expectedSound)));
        CHECK((uint32_t)gba->cpu.gprs[0] == playerAddress && (uint32_t)gba->cpu.gprs[1] == 256);
        destroy(gba);
    }
}

static void lifecycle_guards_retain_complete_images(void) {
    const uint32_t guards[] = {0, MUSIC_READY - 1, MUSIC_READY + 1, MUSIC_READY + 2, UINT32_MAX};
    for (unsigned number = 0x21; number <= 0x24; number++) for (unsigned item = 0; item < 5; item++) {
        struct CupidGBA* gba = machine(0, false);
        open_player(gba, 2);
        put(ram(gba, playerAddress), 52, guards[item]);
        uint8_t expectedPlayer[0x80], expectedTracks[TRACK_BYTES], expectedSound[SOUND_BYTES];
        memcpy(expectedPlayer, ram(gba, playerAddress), sizeof(expectedPlayer));
        memcpy(expectedTracks, ram(gba, trackAddress), sizeof(expectedTracks));
        memcpy(expectedSound, ram(gba, soundAddress), sizeof(expectedSound));
        invoke(gba, number, playerAddress, 0x02008000, 0x11220002, (item & 1) != 0, item & 2 ? MODE_IRQ : MODE_SYS);
        CHECK(!memcmp(expectedPlayer, ram(gba, playerAddress), sizeof(expectedPlayer)));
        CHECK(!memcmp(expectedTracks, ram(gba, trackAddress), sizeof(expectedTracks)));
        CHECK(!memcmp(expectedSound, ram(gba, soundAddress), sizeof(expectedSound)));
        destroy(gba);
    }
}

static uint64_t image_signature(struct CupidGBA* gba) {
    uint8_t image[0x1680];
    memcpy(image, ram(gba, soundAddress), 0x1000);
    memcpy(image + 0x1000, ram(gba, playerAddress), 0x80);
    memcpy(image + 0x1080, ram(gba, trackAddress), 0x600);
    /* These words name internal functions. Their bounds, installation and
     * callable behavior have separate checks; data pointers remain in the image. */
    const unsigned functions[] = {0x20, 0x28, 0x2C, 0x30, 0x38, 0x3C};
    for (unsigned item = 0; item < sizeof(functions)/sizeof(*functions); item++) {
        const uint32_t pointer = word(image, functions[item]);
        if (pointer && pointer < GBA_SIZE_BIOS) put(image, functions[item], 0);
    }
    for (unsigned player = 0; player < 2; player++) {
        const unsigned offset = 0x1038 + player * 64;
        const uint32_t previous = word(image, offset);
        if (previous && previous < GBA_SIZE_BIOS) put(image, offset, 0);
    }
    uint64_t signature = UINT64_C(14695981039346656037);
    for (unsigned byte = 0; byte < sizeof(image); byte++) signature = (signature ^ image[byte]) * UINT64_C(1099511628211);
    return signature;
}

static void prepare_looping_song(struct CupidGBA* gba) {
    uint8_t* song = gba->memory.ewram + 0x8000;
    memset(song, 0, 0x100); song[0] = 1; song[2] = 7;
    put(song, 4, 0x0200C000); put(song, 8, 0x02009000);
    const uint8_t script[] = {0xBB, 75, 0xBD, 0, 0xBE, 127, 0xBF, 64, 0xD4, 60, 100, 0x84, 0xB1};
    memcpy(gba->memory.ewram + 0x9000, script, sizeof(script));
    memset(gba->memory.ewram + 0xC000, 0, 12);
    put(gba->memory.ewram, 0xC004, 0x0200D000); put(gba->memory.ewram, 0xC008, 0x80C040FF);
    uint8_t* wave = gba->memory.ewram + 0xD000;
    memset(wave, 0, 0x220); wave[3] = 0x40;
    put(wave, 4, 0x01000000); put(wave, 8, 32); put(wave, 12, 512);
    for (unsigned sample = 0; sample < 512; sample++) wave[16 + sample] = (uint8_t)(sample * 73 + 19);
    wave[528] = wave[48];
}

static void check_sequence_stage(struct CupidGBA* gba, unsigned stage, unsigned number,
                                 uint32_t a, uint32_t b, uint32_t c) {
    const uint64_t clocks = invoke(gba, number, a, b, c, (stage & 1) != 0, stage & 2 ? MODE_IRQ : MODE_SYS);
    const uint64_t signature = image_signature(gba);
    const struct MusicExpected* expected = &musicSequenceExpected[stage];
    if (clocks != expected->clocks || signature != expected->signature)
        fprintf(stderr, "Music stage %u clocks=%" PRIu64 "/%" PRIu64 " image=%016" PRIx64 "/%016" PRIx64 "\n",
            stage, clocks, expected->clocks, signature, expected->signature);
    CHECK(clocks == expected->clocks && signature == expected->signature);
    if (number == 0x20) CHECK((uint32_t)gba->cpu.gprs[0] == word(ram(gba, soundAddress), 32));
    if (number == 0x1C) CHECK((uint32_t)gba->cpu.gprs[0] == 0x11220008 && (uint32_t)gba->cpu.gprs[1] == 0x11220009);
}

static void song_sequence_matches_complete_images_and_clocks(void) {
    struct CupidGBA* gba = machine(0, false);
    prepare_looping_song(gba);
    check_sequence_stage(gba, 0, 0x20, playerAddress, trackAddress, 2);
    check_sequence_stage(gba, 1, 0x21, playerAddress, 0x02008000, 0x11220002);
    for (unsigned frame = 0; frame < 12; frame++) {
        check_sequence_stage(gba, frame + 2, 0x1C, 0, 0x11220001, 0x11220002);
        invoke(gba, 0x1D, 0, 0x11220001, 0x11220002, false, MODE_SYS);
    }
    check_sequence_stage(gba, 14, 0x22, playerAddress, 0x11220001, 0x11220002);
    check_sequence_stage(gba, 15, 0x23, playerAddress, 0x11220001, 0x11220002);
    check_sequence_stage(gba, 16, 0x24, playerAddress, 2, 0x11220002);
    check_sequence_stage(gba, 17, 0x2A, 0x02012000, 0x11220001, 0x11220002);
    destroy(gba);
}

static void nested_irq_retains_player_and_driver_ownership(void) {
    struct CupidGBA* gba = machine(UINT32_MAX - 200000u, false);
    prepare_looping_song(gba);
    open_player(gba, 2);
    invoke(gba, 0x21, playerAddress, 0x02008000, 0x11220002, false, MODE_SYS);
    /* Timer 1 acknowledges its request, calls Stop while the player is busy,
     * and records both identifiers before returning through the IRQ wrapper. */
    /* PC-relative operands use word indices, including the ARM PC's +8. */
#define IRQ_LITERAL(reg, from, to) (0xE59F0000u | (reg) << 12 | ((to) - (from) - 2) * 4)
    const uint32_t handler[] = {0xE3A00301, 0xE2800C02, 0xE3A01010, 0xE1C010B2,
        IRQ_LITERAL(0, 4, 22), 0xE5902034, IRQ_LITERAL(3, 6, 25), 0xE1520003,
        0x1A00000B, 0xEF220000, IRQ_LITERAL(1, 10, 23), 0xE5912000, 0xE2822001, 0xE5812000,
        IRQ_LITERAL(0, 14, 22), 0xE5902034, 0xE5812004, IRQ_LITERAL(3, 17, 24),
        0xE5933000, 0xE5932000, 0xE5812008, 0xE12FFF1E,
        0x02003000, 0x02003500, 0x03007FF0, MUSIC_READY + 1};
#undef IRQ_LITERAL
    memcpy(gba->memory.iwram + 0x600, handler, sizeof(handler));
    put(gba->memory.iwram, 0x7FFC, 0x03000600);
    memset(gba->memory.ewram + 0x3500, 0, 12);
    cupid_io_write(gba, REG_IE, IRQ_TIMER1); cupid_io_write(gba, REG_IME, 1);
    cupid_io_write(gba, REG_TM1CNT_L, 0xFE00); cupid_io_write(gba, REG_TM1CNT_H, 0xC0);
    CHECK(cupid_timing_global_time(&gba->timing) < UINT32_MAX);
    unmaskedCaller = true;
    CHECK(invoke(gba, 0x1C, 0, 0x11220001, 0x11220002, true, MODE_SYS) > musicSequenceExpected[2].clocks);
    unmaskedCaller = false;
    CHECK(cupid_timing_global_time(&gba->timing) > UINT32_MAX);
    CHECK(word(gba->memory.ewram, 0x3500) >= 2);
    CHECK(word(gba->memory.ewram, 0x3504) == MUSIC_READY + 1 && word(gba->memory.ewram, 0x3508) == MUSIC_READY + 1);
    CHECK(image_signature(gba) == musicSequenceExpected[2].signature);
    destroy(gba);
}

static void exported_track_fields_match_images_and_clocks(void) {
    const uint8_t flags[] = {0, 1, 4, 5, 0x80, 0x85, 0x8F, 0xFF};
    const uint8_t types[] = {0, 1, 2, 255};
    for (unsigned region = 0; region < 2; region++) for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine(0, region != 0); open_player(gba, 2);
        uint32_t entries[36]; read_entries(gba, entries);
        uint8_t initialPlayer[0x80], initialTracks[TRACK_BYTES];
        memcpy(initialPlayer, ram(gba, playerAddress), sizeof(initialPlayer));
        memcpy(initialTracks, ram(gba, trackAddress), sizeof(initialTracks));
        for (unsigned item = 0; item < 24; item++) {
            memcpy(ram(gba, playerAddress), initialPlayer, sizeof(initialPlayer));
            memcpy(ram(gba, trackAddress), initialTracks, sizeof(initialTracks));
            uint8_t* track = ram(gba, trackAddress);
            for (unsigned byte = 1; byte < 32; byte++) track[byte] = (uint8_t)(item * 53 + byte * 37 + 19);
            track[0] = flags[item % 8]; track[24] = types[item % 4];
            const uint32_t arguments[] = {playerAddress, trackAddress, 0x11220002};
            const uint64_t clocks = entry_call(gba, entries[33], arguments, thumb != 0);
            const uint64_t signature = image_signature(gba);
            const struct MusicExpected* expected = &musicTrackExpected[region][thumb][item];
            if (clocks != expected->clocks || signature != expected->signature)
                fprintf(stderr, "Track fields %u/%u/%u clocks=%" PRIu64 "/%" PRIu64 " image=%016" PRIx64 "/%016" PRIx64 "\n",
                    region, thumb, item, clocks, expected->clocks, signature, expected->signature);
            CHECK(clocks == expected->clocks && signature == expected->signature);
        }
        destroy(gba);
    }
}

#include "music-song-inputs.inc"

static uint64_t song_image_signature(struct CupidGBA* gba) {
    uint64_t signature = image_signature(gba);
    for (unsigned byte = 0; byte < 256; byte++) signature = (signature ^ gba->memory.ewram[0x5000 + byte]) * UINT64_C(1099511628211);
    for (unsigned byte = 0; byte < 256; byte++) signature = (signature ^ gba->memory.ewram[0x3500 + byte]) * UINT64_C(1099511628211);
    return signature;
}
static void song_batch(unsigned batch) {
    for (unsigned scene = batch * 2; scene < (batch + 1) * 2; scene++) {
        struct CupidGBA* gba = machine(0, scene == 7);
        music_scene_prepare(gba, scene);
        unsigned depth = 0, repeat = 0, velocity = 0, waves = 0;
        for (unsigned frame = 0; frame < music_scene_frames(scene); frame++) {
            music_scene_event(gba, scene, frame);
            const uint64_t clocks = invoke(gba, 0x1C, 0, 0x11220001, 0x11220002, (frame & 1) != 0, frame & 2 ? MODE_IRQ : MODE_SYS);
            const uint64_t signature = song_image_signature(gba);
            const struct MusicExpected* expected = &musicSongExpected[scene][frame];
            if (clocks != expected->clocks || signature != expected->signature)
                fprintf(stderr, "Song %u frame %u clocks=%" PRIu64 "/%" PRIu64 " image=%016" PRIx64 "/%016" PRIx64 "\n",
                    scene, frame, clocks, expected->clocks, signature, expected->signature);
            CHECK(clocks == expected->clocks && signature == expected->signature);
            uint8_t* tracks = ram(gba, trackAddress); uint8_t* sound = ram(gba, soundAddress);
            depth |= 1u << (tracks[2] & 31); repeat |= 1u << (tracks[3] & 31);
            if (tracks[6] == 70) velocity |= 1;
            if (tracks[6] == 80) velocity |= 2;
            for (unsigned voice = 0; voice < 8; voice++) {
                const uint32_t wave = word(sound, 0x50 + voice * 64 + 36);
                if (wave == 0x0200D000) waves |= 1;
                if (wave == 0x0200D400) waves |= 2;
                if (wave == 0x0200D800) waves |= 4;
            }
            if (scene == 5 && !frame) {
                CHECK(word(sound, 0x50 + 44) == trackAddress + 3 * 80);
                CHECK(word(sound, 0x90 + 44) == trackAddress + 80);
            }
            if (scene == 7) {
                CHECK(word(ram(gba, playerAddress), 52) == MUSIC_READY);
                CHECK(word(ram(gba, playerAddress), 64 + 52) == MUSIC_READY);
                if (frame == 8) CHECK(word(ram(gba, playerAddress), 64 + 4) & 0x80000000);
            }
            invoke(gba, 0x1D, 0, 0x11220001, 0x11220002, false, MODE_SYS);
        }
        if (scene == 2) CHECK(velocity == 3);
        if (scene == 3) CHECK((depth & 3) == 3 && (repeat & 3) == 3);
        if (scene == 6) CHECK(waves == 7);
        if (scene == 7) CHECK((word(ram(gba, playerAddress), 40) & 0xFFFF) == 0);
        if (scene == 9) CHECK((depth & 14) == 14 && !ram(gba, trackAddress)[0]);
        destroy(gba);
    }
}

#include "music-hook-inputs.inc"

static void attached_callbacks_execute_and_retain_ownership(bool thumb) {
    struct CupidGBA* gba = machine(0, false);
    music_hooks_prepare(gba, thumb);
    for (unsigned frame = 0; frame < 12; frame++) {
        music_hooks_event(gba, frame);
        const uint64_t clocks = invoke(gba, 0x1C, 0, 0x11220001, 0x11220002, (frame & 1) != 0, MODE_SYS);
        const struct MusicExpected* expected = &musicHookExpected[thumb][frame];
        CHECK(clocks == expected->clocks && song_image_signature(gba) == expected->signature);
        CHECK(word(ram(gba, soundAddress), 0) == MUSIC_READY);
        CHECK(word(ram(gba, playerAddress), 52) == MUSIC_READY);
        const uint8_t* context = gba->memory.ewram + 0x3500;
        CHECK(word(context, 0) == frame + 1 && word(context, 16) == MUSIC_READY + 1);
        CHECK(word(context, 20) == MUSIC_READY);
        if (!frame) {
            CHECK(word(context, 32) == 4 && word(context, 4 * 32) == 4);
            CHECK(word(context, 32 + 16) == MUSIC_READY + 1);
            CHECK(word(context, 32 + 20) == MUSIC_READY + 1);
            for (unsigned channel = 0; channel < 4; channel++) {
                const uint8_t* state = gba->memory.ewram + 0x5000 + channel * 64;
                CHECK(state[0] == 1 && state[1] == channel + 1);
                CHECK(word(state, 44) == trackAddress + channel * 80);
                CHECK(word(state, 32) == 0x1000 + (channel + 1) * 256 + (60 + channel * 2) * 4);
            }
        }
        if (frame == 6) {
            CHECK(word(context, 2 * 32) == 4);
            CHECK(word(context, 2 * 32 + 16) == MUSIC_READY);
            CHECK(word(context, 2 * 32 + 20) == MUSIC_READY + 1);
            for (unsigned channel = 0; channel < 4; channel++) {
                CHECK(!gba->memory.ewram[0x5000 + channel * 64]);
                CHECK(!word(gba->memory.ewram, 0x5000 + channel * 64 + 44));
                CHECK(!word(ram(gba, trackAddress), channel * 80 + 32));
            }
        }
        invoke(gba, 0x1D, 0, 0x11220001, 0x11220002, false, MODE_SYS);
    }
    const unsigned counts[] = {12, 24, 4, 36, 12, 20};
    for (unsigned hook = 0; hook < 6; hook++) CHECK(word(gba->memory.ewram, 0x3500 + hook * 32) == counts[hook]);
    destroy(gba);
}

struct MusicOutput { uint64_t origin, signature, count, nonzero; };
static void record_music_output(void* context, uint64_t clock, int16_t left, int16_t right) {
    struct MusicOutput* trace = context;
    const uint64_t fields[] = {clock - trace->origin, (uint16_t)left, (uint16_t)right};
    const unsigned widths[] = {8, 2, 2};
    for (unsigned field = 0; field < 3; field++) for (unsigned byte = 0; byte < widths[field]; byte++)
        trace->signature = (trace->signature ^ (uint8_t)(fields[field] >> (byte * 8))) * UINT64_C(1099511628211);
    ++trace->count;
    if (left || right) ++trace->nonzero;
}
static void music_reaches_fifo_and_timestamped_output(unsigned fixture) {
    const unsigned scenes[] = {1, 6, 7};
    struct CupidGBA* gba = machine(fixture == 2 ? UINT32_MAX - 200000u : 0, fixture == 2);
    music_scene_prepare(gba, scenes[fixture]);
    struct MusicOutput trace = {cupid_timing_global_time(&gba->timing), UINT64_C(14695981039346656037), 0, 0};
    gba->audio.sampleSink = record_music_output;
    gba->audio.sampleSinkContext = &trace;
    if (fixture == 2) CHECK(trace.origin < UINT32_MAX);
    cupid_io_write(gba, REG_TM0CNT_H, 0x80);
    for (unsigned frame = 0; frame < 12; frame++) {
        music_scene_event(gba, scenes[fixture], frame);
        invoke(gba, 0x1C, 0, 0x11220001, 0x11220002, (frame & 1) != 0, MODE_SYS);
        const uint64_t deadline = trace.origin + (frame + 1) * UINT64_C(280896);
        unsigned steps = 0;
        while (cupid_timing_global_time(&gba->timing) < deadline && steps < 200000) {
            cupid_gba_step(gba); ++steps;
        }
        CHECK(steps < 200000);
        /* Retain the exact call duration separately from the frame spin's
         * instruction-boundary phase under FIFO/DMA arbitration. */
        const uint64_t finishStart = cupid_timing_global_time(&gba->timing);
        invoke(gba, 0x1D, 0, 0x11220001, 0x11220002, (frame & 1) == 0, MODE_SYS);
        /* Consume the host queue after observing every original timestamp/sample. */
        gba->audio.sampleReadPos = gba->audio.sampleWritePos;
        const struct MusicPlaybackExpected* expected = &musicPlaybackExpected[fixture][frame];
        CHECK(cupid_timing_global_time(&gba->timing) - finishStart == expected->finishClocks);
        CHECK(cupid_timing_global_time(&gba->timing) - trace.origin == expected->elapsed);
        CHECK(song_image_signature(gba) == expected->image);
        CHECK(trace.signature == expected->pcm && trace.count == expected->count && trace.nonzero == expected->nonzero);
        CHECK(!gba->audio.sampleOverruns);
        CHECK(gba->memory.dma[1].nextSource == expected->leftSource);
        CHECK(gba->memory.dma[2].nextSource == expected->rightSource);
        CHECK(gba->audio.chA.latchedSample == expected->leftSample && gba->audio.chB.latchedSample == expected->rightSample);
    }
    CHECK(trace.nonzero > 10000 && gba->timers[0].control == 0x80);
    CHECK(gba->memory.dma[1].reg == 0xB600 && gba->memory.dma[2].reg == 0xB600);
    if (fixture == 2) CHECK(cupid_timing_global_time(&gba->timing) > UINT32_MAX);
    destroy(gba);
}

#include "music-progress.inc"
#include "music-progress-frames.inc"

static void music_bus_progress_survives_dma_timer_and_wrap(void) {
    const uint64_t origins[] = {0, UINT32_MAX - 12u, UINT64_C(0x300000000)};
    for (unsigned fixture = 0; fixture < 3; fixture++) {
        struct CupidGBA* gba = machine(origins[fixture], (fixture & 1) != 0);
        music_scene_header(gba, 0x8000, 16, 7, 0, 0x9000);
        for (unsigned stage = 0; stage < 3; stage++) {
            const struct MusicProgressResult actual = music_partial_service(gba, stage, ((fixture + stage) & 1) != 0);
            const struct MusicProgressResult* expected = &musicProgressExpected[fixture][stage];
            CHECK(actual.clocks == expected->clocks && actual.chronology == expected->chronology && actual.image == expected->image);
        }
        destroy(gba);
    }
}

int main(int argc, char** argv) {
    CHECK(argc == 2 || (argc == 4 && !strcmp(argv[2], "--external-bios")));
    if (argc == 4) externalBios = argv[3];
    if (!strcmp(argv[1], "open")) RUN(opening_preserves_payloads_and_bounds);
    else if (!strcmp(argv[1], "open-guards")) RUN(opening_with_parent_guards);
    else if (!strcmp(argv[1], "discovery")) RUN(discovery_writes_exactly_thirty_six_entries);
    else if (!strcmp(argv[1], "start")) RUN(starting_clips_tracks_and_retains_unrelated_fields);
    else if (!strcmp(argv[1], "stop")) RUN(stopping_preserves_links_and_inactive_owners);
    else if (!strcmp(argv[1], "continue")) RUN(continuation_changes_only_the_pause_bit);
    else if (!strcmp(argv[1], "fade")) RUN(fade_setup_retains_status_and_payload);
    else if (!strcmp(argv[1], "guards")) RUN(lifecycle_guards_retain_complete_images);
    else if (!strcmp(argv[1], "sequence")) RUN(song_sequence_matches_complete_images_and_clocks);
    else if (!strcmp(argv[1], "irq")) RUN(nested_irq_retains_player_and_driver_ownership);
    else if (!strcmp(argv[1], "clear-entry")) RUN(exported_clear_has_complete_writes_and_guards);
    else if (!strcmp(argv[1], "unlink-entry")) RUN(exported_unlink_preserves_other_members);
    else if (!strcmp(argv[1], "controllers-0")) controller_batch(0);
    else if (!strcmp(argv[1], "controllers-1")) controller_batch(1);
    else if (!strcmp(argv[1], "controllers-2")) controller_batch(2);
    else if (!strcmp(argv[1], "controllers-3")) controller_batch(3);
    else if (!strcmp(argv[1], "controllers-4")) controller_batch(4);
    else if (!strcmp(argv[1], "fine-entry")) RUN(fine_aliases_release_and_unlink_all_voices);
    else if (!strcmp(argv[1], "flow-entry")) RUN(flow_entries_preserve_pattern_and_repeat_bounds);
    else if (!strcmp(argv[1], "port-entry")) RUN(port_entry_writes_byte_and_advances_two_operands);
    else if (!strcmp(argv[1], "rate-entry-0")) playback_entry_batch(0);
    else if (!strcmp(argv[1], "rate-entry-1")) playback_entry_batch(1);
    else if (!strcmp(argv[1], "rate-entry-2")) playback_entry_batch(2);
    else if (!strcmp(argv[1], "stop-entry")) RUN(track_stop_entry_retains_player_and_track_status);
    else if (!strcmp(argv[1], "fade-entry")) RUN(fade_entry_keeps_halfword_interval_and_wrapping_level);
    else if (!strcmp(argv[1], "tie-entry")) RUN(tie_entry_releases_only_the_first_matching_live_note);
    else if (!strcmp(argv[1], "source-guard")) RUN(source_guard_keeps_bios_data_boundary);
    else if (!strcmp(argv[1], "track-entry")) RUN(exported_track_fields_match_images_and_clocks);
    else if (!strcmp(argv[1], "songs-0")) song_batch(0);
    else if (!strcmp(argv[1], "songs-1")) song_batch(1);
    else if (!strcmp(argv[1], "songs-2")) song_batch(2);
    else if (!strcmp(argv[1], "songs-3")) song_batch(3);
    else if (!strcmp(argv[1], "songs-4")) song_batch(4);
    else if (!strcmp(argv[1], "hooks-arm")) attached_callbacks_execute_and_retain_ownership(false);
    else if (!strcmp(argv[1], "hooks-thumb")) attached_callbacks_execute_and_retain_ownership(true);
    else if (!strcmp(argv[1], "playback-0")) music_reaches_fifo_and_timestamped_output(0);
    else if (!strcmp(argv[1], "playback-1")) music_reaches_fifo_and_timestamped_output(1);
    else if (!strcmp(argv[1], "playback-2")) music_reaches_fifo_and_timestamped_output(2);
    else if (!strcmp(argv[1], "progress")) RUN(music_bus_progress_survives_dma_timer_and_wrap);
    else CHECK(false);
    printf("Verified %u music-player observations\n", observations);
    return 0;
}
