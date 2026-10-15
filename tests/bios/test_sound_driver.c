/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"
#include "caller.h"
#include "sound-frames.inc"

enum { SOUND_MAGIC = 0x68736D53, AREA_SIZE = 0xFB0, PCM_OFFSET = 0x350 };
static const char* externalBios;
static unsigned observations;
static uint32_t areaAddress;
static bool unmaskedCaller;

static uint32_t word(const uint8_t* data, unsigned offset) {
    uint32_t result; memcpy(&result, data + offset, 4); return result;
}
static void put(uint8_t* data, unsigned offset, uint32_t value) { memcpy(data + offset, &value, 4); }
static uint8_t* area(struct CupidGBA* gba) {
    return areaAddress >> 24 == 2 ? gba->memory.ewram + (areaAddress & 0x3FFFF)
                                : gba->memory.iwram + (areaAddress & 0x7FFF);
}
static struct CupidGBA* machine(uint64_t clock, bool internal) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
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
    areaAddress = internal ? 0x03001000 : 0x02001000;
    memset(area(gba), 0xA5, 0x1000);
    put(gba->memory.iwram, 0x7FF0, areaAddress);
    put(area(gba), 0, SOUND_MAGIC);
    put(area(gba), 0x1C, 0);
    memset(area(gba) + 0x20, 0, 32);
    area(gba)[4] = 1;
    area(gba)[6] = 8;
    area(gba)[7] = 15;
    area(gba)[11] = 3;
    return gba;
}
static void destroy(struct CupidGBA* gba) { cupid_gba_destroy(gba); free(gba); }

static uint64_t call(struct CupidGBA* gba, unsigned service, uint32_t argument,
                     bool thumb, enum CupidPrivMode mode) {
    const uint32_t wide[] = {0xEF000000u | service << 16, 0xEAFFFFFE};
    const uint16_t narrow[] = {(uint16_t)(0xDF00 | service), 0xE7FE};
    memcpy(gba->memory.iwram + 0x100, thumb ? (const void*)narrow : (const void*)wide,
           thumb ? sizeof(narrow) : sizeof(wide));
    cupid_arm_set_priv_mode(&gba->cpu, mode);
    for (unsigned r = 0; r < 15; r++) gba->cpu.gprs[r] = (int32_t)(0x11220000u + r);
    gba->cpu.gprs[0] = (int32_t)argument;
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
    if (steps == 200000) fprintf(stderr, "Sound SWI %02x did not return: clock=%" PRIu64
        " start=%" PRIu64 " pc=%08" PRIx32 " lr=%08" PRIx32 " cpsr=%08" PRIx32 "\n",
        service, cupid_timing_global_time(&gba->timing), start, (uint32_t)gba->cpu.gprs[ARM_PC],
        (uint32_t)gba->cpu.gprs[ARM_LR], (uint32_t)gba->cpu.cpsr.packed);
    CHECK(steps < 200000);
    CHECK(gba->cpu.privilegeMode == mode);
    CHECK((uint32_t)gba->cpu.cpsr.packed == status);
    CHECK((uint32_t)gba->cpu.gprs[2] == 0x11220002);
    for (unsigned r = 4; r < 15; r++)
        CHECK((uint32_t)gba->cpu.gprs[r] == (r == ARM_SP ? 0x03007000u : 0x11220000u + r));
    if (mode == MODE_IRQ) CHECK((uint32_t)gba->cpu.spsr.packed == 0x6500003F);
    observations++;
    return cupid_timing_global_time(&gba->timing) - start;
}

static void mode_fields_and_all_channel_headers(void) {
    for (unsigned kind = 0; kind < 4; kind++) for (unsigned value = 0; value < (kind ? 16u : 256u); value++) {
        struct CupidGBA* gba = machine(0, false);
        uint8_t expected[0x1000]; memcpy(expected, area(gba), sizeof(expected));
        const unsigned shifts[] = {0, 8, 12, 20};
        const uint32_t argument = value << shifts[kind];
        cupid_io_write(gba, REG_SOUNDBIAS, 0x03FF);
        if (!kind && value) expected[5] = value & 127;
        if (kind == 1 && value) {
            expected[6] = (uint8_t)value;
            for (unsigned channel = 0; channel < 12; channel++) expected[0x50 + channel * 64] = 0;
        }
        if (kind == 2 && value) expected[7] = (uint8_t)value;
        call(gba, 0x1B, argument, (value & 1) != 0, value & 2 ? MODE_IRQ : MODE_SYS);
        CHECK(!memcmp(expected, area(gba), sizeof(expected)));
        const unsigned resolution = kind == 3 && (value & 11) ? (value & 3) << 14 : 0;
        CHECK(gba->memory.io[REG_SOUNDBIAS >> 1] == (0x03FF | resolution));
        destroy(gba);
    }

}

static void every_frame_counter_and_access_guard(void) {
    for (unsigned counter = 0; counter < 256; counter++) {
        struct CupidGBA* gba = machine(0, false);
        area(gba)[4] = (uint8_t)counter;
        const uint64_t elapsed = call(gba, 0x1D, 0, (counter & 1) != 0, counter & 2 ? MODE_IRQ : MODE_SYS);
        CHECK(area(gba)[4] == (counter <= 1 ? 3 : counter - 1));
        CHECK(elapsed == (counter <= 1 ? 107u : 83u));
        CHECK(gba->memory.io[REG_DMA1CNT_HI >> 1] == (counter <= 1 ? 0xB600 : 0));
        CHECK(gba->memory.io[REG_DMA2CNT_HI >> 1] == (counter <= 1 ? 0xB600 : 0));
        destroy(gba);
    }
    const uint32_t invalid[] = {0, SOUND_MAGIC - 1, SOUND_MAGIC + 1, SOUND_MAGIC + 2, UINT32_MAX};
    const unsigned services[] = {0x1B, 0x1D, 0x1E, 0x28};
    for (unsigned s = 0; s < sizeof(services)/sizeof(*services); s++)
      for (unsigned i = 0; i < sizeof(invalid)/sizeof(*invalid); i++) {
        if (services[s] == 0x28 && invalid[i] == SOUND_MAGIC + 1) continue;
        struct CupidGBA* gba = machine(0, false);
        put(area(gba), 0, invalid[i]);
        uint8_t expected[0x1000]; memcpy(expected, area(gba), sizeof(expected));
        call(gba, services[s], 0x0000B380, (i & 1) != 0, MODE_SYS);
        CHECK(!memcmp(expected, area(gba), sizeof(expected)));
        CHECK(!gba->memory.io[REG_DMA1CNT_HI >> 1] && !gba->memory.io[REG_DMA2CNT_HI >> 1]);
        destroy(gba);
    }
}

static void clearing_preserves_payloads_and_calls_extensions(void) {
    for (unsigned internal = 0; internal < 2; internal++) for (unsigned callback = 0; callback < 3; callback++) {
        struct CupidGBA* gba = machine(0, internal != 0);
        uint8_t expected[0x1000];
        if (callback) {
            put(area(gba), 0x1C, 0x02003000);
            put(area(gba), 0x2C, 0x03000400 | (callback == 2));
            const uint32_t wide[] = {0xE5C50000, 0xE5972000, 0xE5852004, 0xE12FFF1E};
            const uint16_t narrow[] = {0x7028, 0x683A, 0x606A, 0x4770};
            memcpy(gba->memory.iwram + 0x400, callback == 2 ? (const void*)narrow : (const void*)wide,
                   callback == 2 ? sizeof(narrow) : sizeof(wide));
            memset(gba->memory.ewram + 0x3000, 0xC9, 0x140);
        }
        memcpy(expected, area(gba), sizeof(expected));
        for (unsigned channel = 0; channel < 12; channel++) expected[0x50 + channel * 64] = 0;
        const uint64_t elapsed = call(gba, 0x1E, 0, callback != 0, MODE_IRQ);
        CHECK(!memcmp(expected, area(gba), sizeof(expected)));
        if (!callback && !internal) CHECK(elapsed == 245);
        if (callback) {
            for (unsigned channel = 0; channel < 4; channel++) {
                const uint8_t* record = gba->memory.ewram + 0x3000 + channel * 64;
                CHECK(record[0] == channel + 1 && word(record, 4) == SOUND_MAGIC + 1);
                CHECK(record[1] == 0xC9 && record[8] == 0xC9);
            }
            CHECK(gba->memory.ewram[0x3100] == 0 && gba->memory.ewram[0x3101] == 0xC9);
        }
        destroy(gba);
    }
}

static void pcm_disable_enable_and_banked_callers(void) {
    const uint64_t anchors[] = {0, UINT32_MAX - 8u, UINT64_C(0x300000000)};
    for (unsigned a = 0; a < sizeof(anchors)/sizeof(*anchors); a++)
      for (unsigned internal = 0; internal < 2; internal++) for (unsigned busy = 0; busy < 2; busy++)
        for (unsigned thumb = 0; thumb < 2; thumb++) for (unsigned irq = 0; irq < 2; irq++) {
            struct CupidGBA* gba = machine(anchors[a], internal != 0);
            put(area(gba), 0, SOUND_MAGIC + busy);
            uint8_t expected[0x1000]; memcpy(expected, area(gba), sizeof(expected));
            expected[4] = 0;
            memset(expected + PCM_OFFSET, 0, AREA_SIZE - PCM_OFFSET);
            const uint64_t elapsed = call(gba, 0x28, 0, thumb != 0, irq ? MODE_IRQ : MODE_SYS);
            CHECK(!memcmp(expected, area(gba), sizeof(expected)));
            if (!internal) CHECK(elapsed == 9685);
            CHECK(!gba->memory.io[REG_DMA1CNT_HI >> 1] && !gba->memory.io[REG_DMA2CNT_HI >> 1]);
            call(gba, 0x29, 0, thumb == 0, irq ? MODE_SYS : MODE_IRQ);
            CHECK(!memcmp(expected, area(gba), sizeof(expected)));
            CHECK(gba->memory.io[REG_DMA1CNT_HI >> 1] == 0xB600);
            CHECK(gba->memory.io[REG_DMA2CNT_HI >> 1] == 0xB600);
            destroy(gba);
        }
}

static void playback_rate_batch(unsigned batch) {
    const uint16_t samples[] = {96, 132, 176, 224, 264, 304, 352, 448, 528, 608, 672, 704};
    const uint32_t elapsed[] = {195941, 195942, 195943, 195944, 195944, 195945,
                               195945, 195946, 195946, 195940, 195940, 195940};
    for (unsigned index = batch * 4; index < (batch + 1) * 4; index++) for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine(0, false);
        const unsigned rate = (samples[index] * 597275u + 5000u) / 10000u;
        const uint64_t clocks = call(gba, 0x1B, (index + 1) << 16, thumb != 0, MODE_SYS);
        CHECK(clocks == elapsed[index]);
        CHECK(word(area(gba), 0) == SOUND_MAGIC);
        CHECK(area(gba)[4] == 0 && area(gba)[8] == index + 1 && area(gba)[11] == 1584u / samples[index]);
        CHECK(word(area(gba), 16) == samples[index]);
        CHECK(word(area(gba), 20) == rate && word(area(gba), 24) == ((0x1000000u / rate + 1) >> 1));
        CHECK(gba->timers[0].reload == (uint16_t)(0u - 280896u / samples[index]));
        CHECK(gba->timers[0].control == 0x80 && gba->video.vcount == 159);
        CHECK(gba->memory.io[REG_DMA1CNT_HI >> 1] == 0xB600 && gba->memory.io[REG_DMA2CNT_HI >> 1] == 0xB600);
        for (unsigned byte = PCM_OFFSET; byte < AREA_SIZE; byte++) CHECK(!area(gba)[byte]);
        CHECK(area(gba)[AREA_SIZE] == 0xA5);
        destroy(gba);
    }
}

struct Progress {
    struct CupidTimingEvent event;
    struct CupidGBA* gba;
    uint64_t start, locked, firstPCM, lastPCM, unlocked;
    bool dmaStarted;
};
static void observe_progress(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct Progress* watch = context;
    struct CupidGBA* gba = watch->gba;
    const uint64_t clock = cupid_timing_global_time(timing);
    const uint64_t relative = clock - watch->start;
    if (!watch->locked && word(area(gba), 0) == SOUND_MAGIC + 1) watch->locked = clock;
    if (!watch->firstPCM && !area(gba)[PCM_OFFSET]) {
        watch->firstPCM = clock;
        CHECK(watch->locked && !gba->memory.io[REG_DMA1CNT_HI >> 1] && !gba->memory.io[REG_DMA2CNT_HI >> 1]);
    }
    if (!watch->lastPCM && !area(gba)[AREA_SIZE - 1]) watch->lastPCM = clock;
    if (watch->locked && !watch->unlocked && word(area(gba), 0) == SOUND_MAGIC) watch->unlocked = clock;
    if (!watch->dmaStarted && relative >= 500) {
        watch->dmaStarted = true;
        cupid_io_write(gba, REG_DMA0SAD_LO, 0x2800);
        cupid_io_write(gba, REG_DMA0SAD_HI, 0x0200);
        cupid_io_write(gba, REG_DMA0DAD_LO, 0x2900);
        cupid_io_write(gba, REG_DMA0DAD_HI, 0x0200);
        cupid_io_write(gba, REG_DMA0CNT_LO, 16);
        cupid_io_write(gba, REG_DMA0CNT_HI, 0xC400);
    }
    if (relative < 11000) cupid_timing_schedule(timing, &watch->event, 1);
}
static void bus_progress_timer_dma_and_wrap(void) {
    const uint64_t clocks[] = {0, UINT32_MAX - 12u, UINT64_C(0x300000000)};
    for (unsigned c = 0; c < sizeof(clocks)/sizeof(*clocks); c++) {
        struct CupidGBA* gba = machine(clocks[c], false);
        call(gba, 0x29, 0, true, MODE_SYS);
        for (unsigned i = 0; i < 64; i++) gba->memory.ewram[0x2800 + i] = (uint8_t)(0x3D + i);
        memset(gba->memory.ewram + 0x2900, 0, 64);
        cupid_io_write(gba, REG_TM0CNT_L, 0xFFF0);
        cupid_io_write(gba, REG_TM0CNT_H, 0xC0);
        struct Progress watch = {.gba = gba, .start = cupid_timing_global_time(&gba->timing)};
        watch.event = (struct CupidTimingEvent){.callback = observe_progress,
            .context = &watch, .priority = 255, .name = "Sound bus progress"};
        cupid_timing_schedule(&gba->timing, &watch.event, 1);
        CHECK(call(gba, 0x28, 0, c & 1, MODE_IRQ) > 9685);
        CHECK(watch.locked < watch.firstPCM && watch.firstPCM < watch.lastPCM && watch.lastPCM < watch.unlocked);
        CHECK(watch.dmaStarted && !memcmp(gba->memory.ewram + 0x2800, gba->memory.ewram + 0x2900, 64));
        CHECK(gba->memory.io[REG_IF >> 1] & IRQ_TIMER0);
        CHECK(gba->memory.io[REG_IF >> 1] & IRQ_DMA0);
        CHECK(area(gba)[AREA_SIZE] == 0xA5 && word(area(gba), 0) == SOUND_MAGIC);
        cupid_timing_deschedule(&gba->timing, &watch.event);
        destroy(gba);
    }
}

static void nested_irq_preserves_busy_state(void) {
    /* A real IRQ callback issues a nested VSync while PCM clearing owns the
     * work area. The busy guard must retain the outer counter and identifier. */
    struct CupidGBA* gba = machine(UINT32_MAX - 1000u, false);
    const uint32_t handler[] = {0xE3A00301, 0xE2800C02, 0xE3A01008, 0xE1C010B2,
        0xEF1D0000, 0xE59F1014, 0xE5912000, 0xE2822001, 0xE5812000,
        0xE5902000, 0xE5812004, 0xE12FFF1E, 0x02002A00};
    memcpy(gba->memory.iwram + 0x600, handler, sizeof(handler));
    put(gba->memory.iwram, 0x7FFC, 0x03000600);
    memset(gba->memory.ewram + 0x2A00, 0, 8);
    cupid_io_write(gba, REG_IE, IRQ_TIMER0);
    cupid_io_write(gba, REG_IME, 1);
    cupid_io_write(gba, REG_TM0CNT_L, 0xF000);
    cupid_io_write(gba, REG_TM0CNT_H, 0xC0);
    unmaskedCaller = true;
    CHECK(call(gba, 0x28, 0, true, MODE_SYS) > 9685);
    unmaskedCaller = false;
    CHECK(word(gba->memory.ewram, 0x2A00) >= 2);
    CHECK(word(gba->memory.ewram, 0x2A04) == SOUND_MAGIC + 1);
    CHECK(word(area(gba), 0) == SOUND_MAGIC && area(gba)[4] == 0);
    for (unsigned byte = PCM_OFFSET; byte < AREA_SIZE; byte++) CHECK(!area(gba)[byte]);
    CHECK(area(gba)[AREA_SIZE] == 0xA5);
    destroy(gba);
}

static void initialized_work_area_and_hardware(void) {
    const uint64_t clocks[] = {0, UINT32_MAX - 1000u, UINT64_C(0x300000000)};
    for (unsigned c = 0; c < sizeof(clocks)/sizeof(*clocks); c++)
      for (unsigned internal = 0; internal < 2; internal++)
        for (unsigned thumb = 0; thumb < 2; thumb++) for (unsigned irq = 0; irq < 2; irq++) {
            struct CupidGBA* gba = machine(clocks[c], internal != 0);
            memset(area(gba), 0xA5, 0x1000);
            cupid_io_write(gba, REG_SOUNDBIAS, 0x0200);
            const uint64_t elapsed = call(gba, 0x1A, areaAddress, thumb != 0, irq ? MODE_IRQ : MODE_SYS);
            if (!internal) CHECK(elapsed == 195947);
            CHECK(word(gba->memory.iwram, 0x7FF0) == areaAddress);
            CHECK(word(area(gba), 0) == SOUND_MAGIC);
            CHECK(area(gba)[4] == 0 && area(gba)[5] == 0 && area(gba)[6] == 8 && area(gba)[7] == 15);
            CHECK(area(gba)[8] == 4 && area(gba)[11] == 7);
            CHECK(word(area(gba), 16) == 224 && word(area(gba), 20) == 13379 && word(area(gba), 24) == 627);
            CHECK(!word(area(gba), 28) && !word(area(gba), 32) && !word(area(gba), 36));
            const uint32_t empty = word(area(gba), 40);
            CHECK((empty & 1) && empty < GBA_SIZE_BIOS);
            CHECK(word(area(gba), 44) == empty && word(area(gba), 48) == empty && word(area(gba), 60) == empty);
            CHECK((word(area(gba), 56) & 1) && word(area(gba), 56) < GBA_SIZE_BIOS);
            const uint32_t table = word(area(gba), 52);
            CHECK(!(table & 3) && table + 36 * 4 <= GBA_SIZE_BIOS);
            for (unsigned entry = 0; entry < 36; entry++) CHECK(word(gba->memory.bios, table + entry * 4) < GBA_SIZE_BIOS);
            for (unsigned byte = 0x40; byte < AREA_SIZE; byte++) CHECK(area(gba)[byte] == 0);
            for (unsigned byte = AREA_SIZE; byte < 0x1000; byte++) CHECK(area(gba)[byte] == 0xA5);
            CHECK(gba->memory.dma[1].source == areaAddress + 0x350 && gba->memory.dma[2].source == areaAddress + 0x980);
            CHECK(gba->memory.dma[1].dest == 0x040000A0 && gba->memory.dma[2].dest == 0x040000A4);
            CHECK(gba->memory.dma[1].reg == 0xB600 && gba->memory.dma[2].reg == 0xB600);
            CHECK(gba->memory.io[REG_SOUNDCNT_H >> 1] == 0x210E);
            CHECK(gba->memory.io[REG_SOUNDCNT_X >> 1] & 0x80);
            CHECK(gba->memory.io[REG_SOUNDBIAS >> 1] == 0x4200);
            CHECK(gba->timers[0].reload == 0xFB1A && gba->timers[0].control == 0x80);
            CHECK(gba->video.vcount == 159 && (uint32_t)gba->cpu.gprs[0] == SOUND_MAGIC);
            destroy(gba);
        }
}

static unsigned scene_frames(unsigned scene) { return scene == 6 ? 16 : (scene == 7 ? 12 : 4); }

static void prepare_sound_scene(struct CupidGBA* gba, unsigned scene) {
    cupid_io_write(gba, REG_SOUNDBIAS, 0x0200);
    call(gba, 0x1A, areaAddress, false, MODE_SYS);
    cupid_io_write(gba, REG_TM0CNT_H, 0);
    call(gba, 0x1D, 0, true, MODE_SYS);
    if (!scene) return;
    const unsigned voices = scene == 15 ? 12 : 1;
    area(gba)[6] = scene == 15 ? 12 : 8;
    for (unsigned voice = 0; voice < voices; voice++) {
        uint8_t* channel = area(gba) + 0x50 + voice * 64;
        uint8_t* wave = gba->memory.ewram + 0x5000 + voice * 0x2000;
        unsigned length = 256, loop = 32;
        const bool looping = scene == 3 || scene == 4 || scene == 7 || scene == 8 || scene == 14 || scene == 15;
        if (scene == 6 || scene == 7) length = 4096;
        if (scene == 8) length = 64;
        if (scene == 11) length = 1;
        if (scene == 12) length = 2;
        if (scene == 13) length = 4;
        if (scene == 14) { length = 5; loop = 4; }
        if (scene == 15) { length = 257; loop = voice + 1; }
        memset(wave, 0, length + 17);
        put(wave, 4, 0x01000000); put(wave, 8, loop); put(wave, 12, length);
        for (unsigned sample = 0; sample < length; sample++) wave[16 + sample] = (uint8_t)(sample * 73 + 19 + voice * 37);
        wave[3] = looping ? 0x40 : 0;
        wave[16 + length] = looping ? wave[16 + loop] : 0;
        channel[1] = scene == 1 || scene == 2 || scene == 6 || scene == 7 || scene == 8 ||
            scene == 11 || scene == 12 || (scene == 15 && !(voice & 1)) ? 8 : 0;
        channel[2] = (uint8_t)(255 - voice * 7); channel[3] = (uint8_t)(128 + voice * 11);
        channel[4] = scene == 2 ? 64 : (scene == 6 ? 32 : (scene == 8 ? 0 : 255));
        channel[5] = 192; channel[6] = 64; channel[7] = 128;
        if (scene == 7) { channel[12] = 16; channel[13] = 4; }
        unsigned frequency = scene == 4 ? 7000 : 13379;
        if (scene == 10) frequency = 1337901;
        if (scene == 13) frequency = 50000;
        if (scene == 14) frequency = 60000;
        if (scene == 15) frequency = 1000 + voice * 4000;
        put(channel, 32, frequency);
        put(channel, 36, 0x02005000 + voice * 0x2000);
        channel[0] = scene == 9 ? 0xC0 : 0x80;
    }
    if (scene == 5 || scene == 15) area(gba)[5] = 80;
}

static uint64_t scene_signature(struct CupidGBA* gba) {
    uint64_t value = UINT64_C(14695981039346656037);
    for (unsigned byte = 0; byte < 0x1000; byte++) {
        /* Six reserved words contain firmware-internal addresses. Supported
         * hooks and bounds are checked separately; all other bytes participate. */
        if (byte >= 0x28 && byte < 0x40) continue;
        value = (value ^ area(gba)[byte]) * UINT64_C(1099511628211);
    }
    return value;
}

static void sound_scene_batch(unsigned batch) {
    for (unsigned scene = batch * 4; scene < (batch + 1) * 4; scene++) {
        struct CupidGBA* gba = machine(0, false);
        prepare_sound_scene(gba, scene);
        for (unsigned frame = 0; frame < scene_frames(scene); frame++) {
            if (scene == 7 && frame == 2) area(gba)[0x50] |= 0x40;
            const struct SoundFrameExpected* expected = &soundFrameExpected[scene][frame];
            const uint64_t clocks = call(gba, 0x1C, 0, (frame & 1) != 0, frame & 2 ? MODE_IRQ : MODE_SYS);
            const uint64_t signature = scene_signature(gba);
            if (clocks != expected->clocks || signature != expected->signature)
                fprintf(stderr, "Sound scene %u frame %u clocks=%" PRIu64 "/%" PRIu64
                    " signature=%016" PRIx64 "/%016" PRIx64 "\n", scene, frame,
                    clocks, expected->clocks, signature, expected->signature);
            CHECK(clocks == expected->clocks && signature == expected->signature);
            CHECK((uint32_t)gba->cpu.gprs[0] == 0x11220008 && (uint32_t)gba->cpu.gprs[1] == 0x11220009);
            call(gba, 0x1D, 0, false, MODE_SYS);
        }
        destroy(gba);
    }
}

static void mixing_in_internal_ram(void) {
    const uint64_t anchors[] = {UINT32_MAX - 200000u, UINT64_C(0x300000000)};
    const unsigned clocks[] = {125317, 125252, 125230, 125232};
    for (unsigned anchor = 0; anchor < 2; anchor++) {
        struct CupidGBA* gba = machine(anchors[anchor], true);
        prepare_sound_scene(gba, 15);
        for (unsigned frame = 0; frame < 4; frame++) {
            if (!anchor && !frame) CHECK(cupid_timing_global_time(&gba->timing) < UINT32_MAX);
            CHECK(call(gba, 0x1C, 0, (frame & 1) != 0, frame & 2 ? MODE_IRQ : MODE_SYS) == clocks[frame]);
            if (!anchor && !frame) CHECK(cupid_timing_global_time(&gba->timing) > UINT32_MAX);
            CHECK(scene_signature(gba) == soundFrameExpected[15][frame].signature);
            call(gba, 0x1D, 0, false, MODE_SYS);
        }
        destroy(gba);
    }
}

static void mixing_at_all_playback_rates(void) {
    for (unsigned rate = 1; rate <= 12; rate++) {
        struct CupidGBA* gba = machine(0, false);
        prepare_sound_scene(gba, 15);
        call(gba, 0x1B, rate << 16, false, MODE_SYS);
        cupid_io_write(gba, REG_TM0CNT_H, 0);
        for (unsigned voice = 0; voice < 12; voice++) area(gba)[0x50 + voice * 64] = 0x80;
        call(gba, 0x1D, 0, true, MODE_SYS);
        for (unsigned frame = 0; frame < 3; frame++) {
            const struct SoundFrameExpected* expected = &soundRateFrameExpected[rate - 1][frame];
            CHECK(call(gba, 0x1C, 0, (frame & 1) != 0, MODE_SYS) == expected->clocks);
            CHECK(scene_signature(gba) == expected->signature);
            call(gba, 0x1D, 0, false, MODE_SYS);
        }
        destroy(gba);
    }
}

static void frame_hooks_and_access_guards(void) {
    const uint32_t callback[] = {0xE5901000, 0xE2811001, 0xE5801000, 0xE59F1014,
        0xE5911000, 0xE5912000, 0xE5802004, 0xE3A0205A, 0xE5812040, 0xE12FFF1E, 0x03007FF0};
    const uint16_t extension[] = {0x6801, 0x6441, 0x6C01, 0x6481, 0x4770};
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine(0, false);
        prepare_sound_scene(gba, 0);
        memcpy(gba->memory.iwram + 0x400, callback, sizeof(callback));
        memcpy(gba->memory.iwram + 0x500, extension, sizeof(extension));
        memset(gba->memory.ewram + 0x3000, 0, 8);
        put(area(gba), 32, 0x03000400); put(area(gba), 36, 0x02003000); put(area(gba), 40, 0x03000501);
        call(gba, 0x1C, 0, thumb != 0, thumb ? MODE_IRQ : MODE_SYS);
        CHECK(word(gba->memory.ewram, 0x3000) == 1 && word(gba->memory.ewram, 0x3004) == SOUND_MAGIC + 1);
        CHECK(word(area(gba), 0x40) == 0x5A && word(area(gba), 0x44) == SOUND_MAGIC + 1 && word(area(gba), 0x48) == 0x5A);
        CHECK(word(area(gba), 0) == SOUND_MAGIC);
        put(area(gba), 0, SOUND_MAGIC + 1);
        const uint64_t before = scene_signature(gba);
        CHECK(call(gba, 0x1C, 0, thumb == 0, MODE_SYS) == 70);
        CHECK(scene_signature(gba) == before && word(gba->memory.ewram, 0x3000) == 1);
        destroy(gba);
    }
}

static void mixing_survives_nested_irq(void) {
    struct CupidGBA* gba = machine(UINT32_MAX - 200000u, false);
    prepare_sound_scene(gba, 1);
    const uint32_t handler[] = {0xE3A00301, 0xE2800C02, 0xE3A01010, 0xE1C010B2,
        0xEF1D0000, 0xE59F1014, 0xE5912000, 0xE2822001, 0xE5812000,
        0xE5902000, 0xE5812004, 0xE12FFF1E, 0x02003100};
    memcpy(gba->memory.iwram + 0x600, handler, sizeof(handler));
    put(gba->memory.iwram, 0x7FFC, 0x03000600);
    memset(gba->memory.ewram + 0x3100, 0, 8);
    cupid_io_write(gba, REG_IE, IRQ_TIMER1);
    cupid_io_write(gba, REG_IME, 1);
    cupid_io_write(gba, REG_TM1CNT_L, 0xF000);
    cupid_io_write(gba, REG_TM1CNT_H, 0xC0);
    unmaskedCaller = true;
    CHECK(cupid_timing_global_time(&gba->timing) < UINT32_MAX);
    CHECK(call(gba, 0x1C, 0, true, MODE_SYS) > soundFrameExpected[1][0].clocks);
    CHECK(cupid_timing_global_time(&gba->timing) > UINT32_MAX);
    unmaskedCaller = false;
    CHECK(word(gba->memory.ewram, 0x3100) >= 2 && word(gba->memory.ewram, 0x3104) == SOUND_MAGIC + 1);
    CHECK(scene_signature(gba) == soundFrameExpected[1][0].signature);
    destroy(gba);
}

struct SoundOutputTrace { uint64_t origin, signature, count, nonzero; };
static void record_sound_output(void* context, uint64_t clock, int16_t left, int16_t right) {
    struct SoundOutputTrace* trace = context;
    const uint64_t fields[] = {clock - trace->origin, (uint16_t)left, (uint16_t)right};
    const unsigned widths[] = {8, 2, 2};
    for (unsigned field = 0; field < 3; field++) for (unsigned byte = 0; byte < widths[field]; byte++)
        trace->signature = (trace->signature ^ (uint8_t)(fields[field] >> (byte * 8))) * UINT64_C(1099511628211);
    ++trace->count;
    if (left || right) ++trace->nonzero;
}

static void playback_reaches_fifo_and_output(void) {
    /* Independent observations use the same owned wave inputs, real timer
     * overflows, FIFO refill bursts and twelve complete frame periods. */
    const unsigned scenes[] = {3, 15};
    const uint64_t origins[] = {0, UINT32_MAX - 200000u};
    const uint64_t outputs[] = {UINT64_C(0xC1BA4C9839934FFE), UINT64_C(0x883CE24630E02C68)};
    const uint64_t areas[] = {UINT64_C(0x0769CB27762A2CB4), UINT64_C(0x387119929D02A8A3)};
    const uint64_t elapsed[] = {3370836, 3370837};
    const unsigned nonzero[] = {11772, 11978};
    for (unsigned fixture = 0; fixture < 2; fixture++) {
        struct CupidGBA* gba = machine(origins[fixture], false);
        prepare_sound_scene(gba, scenes[fixture]);
        struct SoundOutputTrace trace = {cupid_timing_global_time(&gba->timing), UINT64_C(14695981039346656037), 0, 0};
        gba->audio.sampleSink = record_sound_output;
        gba->audio.sampleSinkContext = &trace;
        if (fixture) CHECK(trace.origin < UINT32_MAX);
        cupid_io_write(gba, REG_TM0CNT_H, 0x80);
        for (unsigned frame = 0; frame < 12; frame++) {
            call(gba, 0x1C, 0, (frame & 1) != 0, MODE_SYS);
            const uint64_t deadline = trace.origin + (frame + 1) * UINT64_C(280896);
            unsigned steps = 0;
            while (cupid_timing_global_time(&gba->timing) < deadline && steps < 200000) {
                cupid_gba_step(gba); ++steps;
            }
            CHECK(steps < 200000);
            call(gba, 0x1D, 0, (frame & 1) == 0, MODE_SYS);
            /* Model the host consuming each frame without changing samples. */
            gba->audio.sampleReadPos = gba->audio.sampleWritePos;
        }
        CHECK(trace.count == 13168 && trace.nonzero == nonzero[fixture]);
        CHECK(trace.signature == outputs[fixture] && scene_signature(gba) == areas[fixture]);
        CHECK(cupid_timing_global_time(&gba->timing) - trace.origin == elapsed[fixture]);
        if (fixture) CHECK(cupid_timing_global_time(&gba->timing) > UINT32_MAX);
        CHECK(!gba->audio.sampleOverruns && area(gba)[4] == 2);
        CHECK(gba->memory.dma[1].nextSource == areaAddress + 0x7B0);
        CHECK(gba->memory.dma[2].nextSource == areaAddress + 0xDE0);
        CHECK(gba->audio.chA.latchedSample != 0 && gba->audio.chB.latchedSample != 0);
        destroy(gba);
    }
}

int main(int argc, char** argv) {
    CHECK(argc == 2 || (argc == 4 && !strcmp(argv[2], "--external-bios")));
    if (argc == 4) externalBios = argv[3];
    if (!strcmp(argv[1], "mode")) RUN(mode_fields_and_all_channel_headers);
    else if (!strcmp(argv[1], "vsync")) RUN(every_frame_counter_and_access_guard);
    else if (!strcmp(argv[1], "clear")) RUN(clearing_preserves_payloads_and_calls_extensions);
    else if (!strcmp(argv[1], "pcm")) RUN(pcm_disable_enable_and_banked_callers);
    else if (!strcmp(argv[1], "rates-0")) playback_rate_batch(0);
    else if (!strcmp(argv[1], "rates-1")) playback_rate_batch(1);
    else if (!strcmp(argv[1], "rates-2")) playback_rate_batch(2);
    else if (!strcmp(argv[1], "init")) RUN(initialized_work_area_and_hardware);
    else if (!strcmp(argv[1], "frames-0")) sound_scene_batch(0);
    else if (!strcmp(argv[1], "frames-1")) sound_scene_batch(1);
    else if (!strcmp(argv[1], "frames-2")) sound_scene_batch(2);
    else if (!strcmp(argv[1], "frames-3")) { sound_scene_batch(3); RUN(mixing_in_internal_ram); }
    else if (!strcmp(argv[1], "hooks")) RUN(frame_hooks_and_access_guards);
    else if (!strcmp(argv[1], "mix-irq")) RUN(mixing_survives_nested_irq);
    else if (!strcmp(argv[1], "playback")) RUN(playback_reaches_fifo_and_output);
    else if (!strcmp(argv[1], "mix-rates")) RUN(mixing_at_all_playback_rates);
    else if (!strcmp(argv[1], "chronology")) {
        RUN(bus_progress_timer_dma_and_wrap);
        RUN(nested_irq_preserves_busy_state);
    }
    else CHECK(false);
    printf("Verified %u sound-driver observations\n", observations);
    return 0;
}
