/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../../test.h"

static const uint64_t origins[] = {0, INT32_MAX - 8u, UINT32_MAX - 8u,
                                  UINT64_C(0x300000000) - 8};
static unsigned scenes;

static struct CupidGBA* machine(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    return gba;
}

static void clock_at(struct CupidGBA* gba, uint64_t clock) {
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
}

static void control(struct CupidGBA* gba, unsigned bank, unsigned size, unsigned access) {
    uint8_t value = (uint8_t)(0x80 | (bank << 6) | (size << 5));
    if (access == 1) cupid_io_write8(gba, REG_SOUND3CNT_L, value);
    else cupid_io_write(gba, REG_SOUND3CNT_L, value);
}

static void start(struct CupidGBA* gba, uint64_t origin, const uint8_t* ram,
                  unsigned bank, unsigned size, unsigned access) {
    clock_at(gba, origin);
    gba->audio.lastSample = gba->audio.sampleOrigin = origin;
    gba->audio.nextSample = origin + UINT64_C(0x10000000000);
    cupid_io_write(gba, REG_SOUNDCNT_X, 0);
    cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
    for (unsigned target = 0; target < 2; ++target) {
        /* The CPU accesses the bank opposite the programmed playback bank. */
        cupid_io_write(gba, REG_SOUND3CNT_L, (uint16_t)((target ^ 1) << 6));
        for (unsigned byte = 0; byte < 16; byte += access) {
            unsigned value = ram[target * 16 + byte];
            if (access == 1) cupid_io_write8(gba, REG_WAVE_RAM0_LO + byte, (uint8_t)value);
            else cupid_io_write(gba, REG_WAVE_RAM0_LO + byte,
                    (uint16_t)(value | (ram[target * 16 + byte + 1] << 8)));
        }
    }
    CHECK(!memcmp(gba->audio.psg.ch3.wavedata8, ram, 32));
    control(gba, bank, size, access);
    cupid_io_write(gba, REG_SOUND3CNT_H, 0x2000);
    cupid_io_write(gba, REG_SOUND3CNT_X, 0x87FF);
}

static unsigned nibble(const uint8_t* ram, unsigned index) {
    return (ram[index >> 1] >> ((index & 1) ? 0 : 4)) & 15;
}

static void selected_bank_plays_first(void) {
    struct CupidGBA* gba = machine();
    for (unsigned epoch = 0; epoch < 4; ++epoch)
    for (unsigned left = 0; left < 16; ++left)
    for (unsigned right = 0; right < 16; ++right)
    for (unsigned bank = 0; bank < 2; ++bank)
    for (unsigned size = 0; size < 2; ++size)
    for (unsigned access = 1; access <= 2; ++access) {
        uint8_t ram[32];
        memset(ram, (int)(left * 17), 16);
        memset(ram + 16, (int)(right * 17), 16);
        uint64_t origin = origins[epoch];
        start(gba, origin, ram, bank, size, access);
        for (unsigned step = 0; step < 64; ++step) {
            uint64_t edge = origin + 32 + step * 8;
            clock_at(gba, edge);
            cupid_audio_psg_run(&gba->audio.psg, edge, 4);
            unsigned playingBank = bank ^ (size ? ((step >> 5) & 1) : 0);
            CHECK(gba->audio.psg.ch3.sample == (int)(playingBank ? right : left));
            CHECK(gba->audio.psg.ch3.rawSample == (playingBank ? right : left));
            CHECK(gba->audio.psg.ch3.nextUpdate == edge + 8);
        }
        CHECK(!memcmp(gba->audio.psg.ch3.wavedata8, ram, 32));
        ++scenes;
    }
    cupid_gba_destroy(gba);
    free(gba);
}

static void rotate(uint8_t* ram, unsigned bank, unsigned size) {
    unsigned first = size ? 0 : bank * 16, count = size ? 32 : 16;
    uint8_t copy[32];
    memcpy(copy, ram + first, count);
    for (unsigned i = 0; i < count; ++i)
        ram[first + i] = (uint8_t)((copy[i] << 4) | (copy[(i + 1) % count] >> 4));
}

static void patterns_preserve_rotation_and_cpu_window(void) {
    struct CupidGBA* gba = machine();
    for (unsigned epoch = 0; epoch < 4; ++epoch)
    for (unsigned pattern = 0; pattern < 16; ++pattern)
    for (unsigned bank = 0; bank < 2; ++bank)
    for (unsigned size = 0; size < 2; ++size)
    for (unsigned access = 1; access <= 2; ++access) {
        uint8_t initial[32], expected[32];
        for (unsigned i = 0; i < 32; ++i) initial[i] = (uint8_t)(pattern * 17 + i * 29);
        memcpy(expected, initial, 32);
        uint64_t origin = origins[epoch];
        start(gba, origin, initial, bank, size, access);
        for (unsigned step = 0; step < 128; ++step) {
            uint64_t edge = origin + 32 + step * 8;
            clock_at(gba, edge);
            cupid_audio_psg_run(&gba->audio.psg, edge, 4);
            unsigned index = size ? (bank * 32 + step) & 63 : bank * 32 + (step & 31);
            CHECK(gba->audio.psg.ch3.sample == (int)nibble(initial, index));
            rotate(expected, bank, size);
            CHECK(!memcmp(gba->audio.psg.ch3.wavedata8, expected, 32));
            for (unsigned byte = 0; byte < 16; byte += 2) {
                unsigned address = (bank ^ 1) * 16 + byte;
                CHECK(cupid_audio_read_register(&gba->audio, REG_WAVE_RAM0_LO + byte) ==
                      (expected[address] | (expected[address + 1] << 8)));
            }
        }
        ++scenes;
    }
    cupid_gba_destroy(gba);
    free(gba);
}

static void bank_writes_select_the_next_sample(void) {
    struct CupidGBA* gba = machine();
    for (unsigned epoch = 0; epoch < 4; ++epoch)
    for (unsigned pattern = 0; pattern < 16; ++pattern)
    for (unsigned initialBank = 0; initialBank < 2; ++initialBank)
    for (unsigned access = 1; access <= 2; ++access) {
        uint8_t initial[32], expected[32];
        for (unsigned i = 0; i < 32; ++i) initial[i] = (uint8_t)(pattern * 17 + i * 29);
        memcpy(expected, initial, 32);
        uint64_t origin = origins[epoch];
        start(gba, origin, initial, initialBank, 1, access);
        for (unsigned step = 0; step < 128; ++step) {
            unsigned bank = initialBank ^ ((step / 7) & 1);
            uint64_t edge = origin + 32 + step * 8;
            clock_at(gba, edge - 1);
            int held = gba->audio.psg.ch3.sample;
            control(gba, bank, 1, access);
            CHECK(gba->audio.psg.ch3.sample == held);
            CHECK(gba->audio.psg.ch3.nextUpdate == edge);
            CHECK(!memcmp(gba->audio.psg.ch3.wavedata8, expected, 32));
            clock_at(gba, edge);
            cupid_audio_psg_run(&gba->audio.psg, edge, 4);
            CHECK(gba->audio.psg.ch3.sample == (int)nibble(initial, (bank * 32 + step) & 63));
            rotate(expected, bank, 1);
            CHECK(!memcmp(gba->audio.psg.ch3.wavedata8, expected, 32));
        }
        ++scenes;
    }
    cupid_gba_destroy(gba);
    free(gba);
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "first")) RUN(selected_bank_plays_first);
    else if (!strcmp(argv[1], "patterns")) RUN(patterns_preserve_rotation_and_cpu_window);
    else if (!strcmp(argv[1], "switches")) RUN(bank_writes_select_the_next_sample);
    else CHECK(false);
    printf("PASS %u wave-bank scenes\n", scenes);
    return 0;
}
