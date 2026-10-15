/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"
#include "caller.h"

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
    else if (!strcmp(argv[1], "chronology")) {
        RUN(bus_progress_timer_dma_and_wrap);
        RUN(nested_irq_preserves_busy_state);
    }
    else CHECK(false);
    printf("Verified %u sound-driver observations\n", observations);
    return 0;
}
