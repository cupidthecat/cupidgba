/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

static const char* externalBios;
static uint8_t externalImage[GBA_SIZE_BIOS];
static bool externalImageLoaded;
static unsigned observations;
static unsigned firstGroup, lastGroup = 15;
static bool interactionSelection;

struct ResetTrace {
    struct CupidGBA* gba;
    struct CupidTimingEvent sample, dma;
    uint64_t times[32];
    uint32_t values[32][3];
    unsigned count;
};

static uint32_t word(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void observe_clear(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct ResetTrace* trace = context;
    const uint32_t values[] = {word(trace->gba->memory.ewram), word(trace->gba->memory.ewram + 0x2000),
        word(trace->gba->memory.ewram + 0x30000)};
    if (!trace->count || memcmp(trace->values[trace->count-1], values, sizeof(values))) {
        CHECK(trace->count < 32);
        memcpy(trace->values[trace->count], values, sizeof(values));
        trace->times[trace->count++] = cupid_timing_global_time(timing);
    }
    cupid_timing_schedule(timing, &trace->sample, 64);
}

static void start_dma(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing;
    (void)late;
    cupid_io_write(context, REG_DMA0CNT_HI, 0xC400);
}

struct Result {
    uint64_t clocks;
    uint32_t registers[15], cpsr;
    uint8_t ewram[GBA_SIZE_EWRAM], iwram[GBA_SIZE_IWRAM];
    uint8_t palette[GBA_SIZE_PALETTE], vram[GBA_SIZE_VRAM], oam[GBA_SIZE_OAM], wave[32];
    uint16_t io[0x210 / 2];
    struct CupidAudioFIFO fifo[2];
};

static void uniform(const uint8_t* data, unsigned size, uint8_t value) {
    for (unsigned i = 0; i < size; i++) CHECK(data[i] == value);
}

static struct CupidGBA* machine(const char* bios) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    if (bios) {
        if (!externalImageLoaded) {
            CHECK(cupid_load_bios(gba, bios));
            memcpy(externalImage, gba->memory.bios, sizeof(externalImage));
            externalImageLoaded = true;
        } else {
            memcpy(gba->memory.bios, externalImage, sizeof(externalImage));
            gba->memory.fullBios = 1;
            gba->memory.biosLoaded = true;
            gba->hasBios = true;
            gba->biosHLE = false;
        }
    }
    cupid_gba_reset(gba);
    /* Both paths start at the same display phase rather than booting an input. */
    cupid_video_reset(&gba->video);
    cupid_io_write(gba, REG_POSTFLG, 1);
    return gba;
}

/* Configuration: Thumb, EWRAM/IWRAM caller, fast cartridge timing. */
static struct Result* execute(uint32_t flags, unsigned configuration, const char* bios, struct ResetTrace* trace) {
    struct CupidGBA* gba = machine(bios);
    const uint8_t pattern = 0x5A;
    memset(gba->memory.ewram, pattern, GBA_SIZE_EWRAM);
    memset(gba->memory.iwram, pattern, GBA_SIZE_IWRAM);
    memset(gba->video.palette, pattern, GBA_SIZE_PALETTE);
    memset(gba->video.vram, pattern, GBA_SIZE_VRAM);
    memset(gba->video.oam.raw, pattern, GBA_SIZE_OAM);
    cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
    cupid_io_write(gba, REG_SOUND3CNT_L, 0x60);
    memset(gba->audio.psg.ch3.wavedata8, pattern, 32);
    cupid_io_write(gba, REG_SOUNDBIAS, 0xC156);
    /* Seed readable 32-bit data before testing serial retention/reset. */
    cupid_io_write(gba, REG_RCNT, 0);
    cupid_io_write(gba, REG_SIOCNT, 0x1000);
    cupid_io_write(gba, REG_SIODATA32_LO, 0xABCD);
    cupid_io_write(gba, REG_SIODATA32_HI, 0x1234);
    CHECK(cupid_io_read(gba, REG_SIODATA32_LO) == 0xABCD);
    CHECK(cupid_io_read(gba, REG_SIODATA32_HI) == 0x1234);
    cupid_io_write(gba, REG_KEYCNT, 0x8201);
    const bool interactions = configuration & 16;
    gba->irqf = interactions ? 0 : 0x3FFF;
    const bool thumb = configuration & 1;
    const unsigned region = (configuration >> 1) & 3;
    uint8_t* code;
    uint32_t address;
    if (!region) {
        gba->memory.rom = calloc(1, 4096);
        CHECK(gba->memory.rom);
        gba->memory.romSize = 4096;
        gba->memory.romMask = 4095;
        code = gba->memory.rom;
        address = GBA_BASE_ROM0;
        cupid_io_write(gba, REG_WAITCNT, configuration & 8 ? 0x4317 : 0);
    } else if (region == 1) {
        CHECK(!(flags & 1));
        code = gba->memory.ewram + 0x3F000;
        address = GBA_BASE_EWRAM + 0x3F000;
    } else {
        CHECK(region == 2);
        code = gba->memory.iwram + 0x7E40;
        address = GBA_BASE_IWRAM + 0x7E40;
    }
    const uint32_t arm[] = {0xEF010000, 0xEAFFFFFE};
    const uint16_t narrow[] = {0xDF01, 0xE7FE};
    memcpy(code, thumb ? (const void*)narrow : (const void*)arm,
           thumb ? sizeof(narrow) : sizeof(arm));
    for (unsigned r = 0; r < 15; r++) gba->cpu.gprs[r] = (int32_t)(0x11220000 + r);
    gba->cpu.gprs[ARM_SP] = GBA_SP_BASE_SYSTEM;
    gba->cpu.gprs[0] = (int32_t)flags;
    gba->cpu.cpsr.packed = interactions ? 0xA000001F : 0xA000009F;
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = (int32_t)address;
    if (thumb) CupidThumbWritePC(&gba->cpu);
    else CupidARMWritePC(&gba->cpu);
    cupid_io_write(gba, REG_TM0CNT_L, interactions ? 0xF800 : 0);
    cupid_io_write(gba, REG_TM0CNT_H, interactions ? 0xC0 : 0x80);
    cupid_timing_tick(&gba->timing, 2);
    const unsigned callerRegion = address >> 24;
    const unsigned fetch = 1 + (thumb ? gba->memory.waitstatesSeq16[callerRegion] : gba->memory.waitstatesSeq32[callerRegion]);
    const unsigned data = 1 + gba->memory.waitstatesNonseq16[callerRegion];
    /* The fixed reset prefix takes 427 clocks besides the caller bus accesses. */
    const unsigned stoppedCounter = 427 + fetch + data;
    if (interactions) {
        CHECK(trace && flags == 1 && region == 0);
        memset(trace, 0, sizeof(*trace));
        trace->gba = gba;
        trace->sample.context = trace;
        trace->sample.callback = observe_clear;
        trace->sample.priority = 0x60;
        cupid_timing_schedule(&gba->timing, &trace->sample, 64);
        const uint32_t handler[] = {
            0xE3A00301, 0xE2800C02, 0xE1D010B2, 0xE1C010B2,
            0xE59F2018, 0xE5923000, 0xE2833001, 0xE5823000,
            0xE5923004, 0xE1833001, 0xE5823004, 0xE12FFF1E,
            GBA_BASE_IWRAM + 0x7E20
        };
        memcpy(gba->memory.rom + 0x800, handler, sizeof(handler));
        const uint32_t entry = GBA_BASE_ROM0 + 0x800, replacement = 0x11224488;
        memcpy(gba->memory.iwram + 0x7FFC, &entry, sizeof(entry));
        memset(gba->memory.iwram + 0x7E20, 0, 8);
        memcpy(gba->memory.iwram + 0x7E30, &replacement, sizeof(replacement));
        cupid_io_write(gba, REG_DMA0SAD_LO, 0x7E30);
        cupid_io_write(gba, REG_DMA0SAD_HI, 0x0300);
        cupid_io_write(gba, REG_DMA0DAD_LO, 0x2000);
        cupid_io_write(gba, REG_DMA0DAD_HI, 0x0200);
        cupid_io_write(gba, REG_DMA0CNT_LO, 1);
        trace->dma.context = gba;
        trace->dma.callback = start_dma;
        trace->dma.priority = 0x20;
        cupid_timing_schedule(&gba->timing, &trace->dma, 10000);
        cupid_io_write(gba, REG_IE, IRQ_TIMER0 | IRQ_DMA0);
        cupid_io_write(gba, REG_IME, 1);
    }
    const uint64_t start = cupid_timing_global_time(&gba->timing);
    unsigned steps;
    for (steps = 0; steps < 100000; steps++) {
        cupid_gba_step(gba);
        if (gba->cpu.executionMode == (thumb ? MODE_THUMB : MODE_ARM) &&
            (uint32_t)gba->cpu.gprs[ARM_PC] == address + (thumb ? 4 : 8)) break;
    }
    CHECK(steps < 100000);
    struct Result* result = calloc(1, sizeof(*result));
    CHECK(result);
    result->clocks = cupid_timing_global_time(&gba->timing) - start;
    CHECK(result->clocks > 100);
    CHECK(gba->cpu.privilegeMode == MODE_SYS);
    CHECK((uint32_t)gba->cpu.cpsr.packed == ((interactions ? 0xA000001F : 0xA000009F) | (thumb ? 0x20u : 0)));
    const uint32_t r0 = flags & 0x7F ? GBA_SP_BASE_SYSTEM - 32 : flags & 0x80 ? 0x100 : flags;
    CHECK((uint32_t)gba->cpu.gprs[0] == r0);
    CHECK((uint32_t)gba->cpu.gprs[1] == GBA_BASE_IWRAM + (flags & 2 ? 0x7E00 : 0));
    CHECK((uint32_t)gba->cpu.gprs[2] == 0x11220002);
    CHECK((uint32_t)gba->cpu.gprs[3] == 0x170);
    for (unsigned r = 4; r < 15; r++) {
        CHECK((uint32_t)gba->cpu.gprs[r] == (r == ARM_SP ? GBA_SP_BASE_SYSTEM : 0x11220000 + r));
    }
    const uint8_t* areas[] = {gba->memory.ewram, gba->memory.iwram, (const uint8_t*)gba->video.palette,
        (const uint8_t*)gba->video.vram, (const uint8_t*)gba->video.oam.raw};
    const unsigned sizes[] = {GBA_SIZE_EWRAM, GBA_SIZE_IWRAM - 0x200, GBA_SIZE_PALETTE, GBA_SIZE_VRAM, GBA_SIZE_OAM};
    for (unsigned a = 0; a < 5; a++) {
        if (a == 0 && region == 1) {
            uniform(areas[a], 0x3F000, pattern);
            uniform(areas[a] + 0x3F008, sizes[a] - 0x3F008, pattern);
        } else uniform(areas[a], sizes[a], flags & (1u << a) ? 0 : pattern);
    }
    /* Bulk clearing leaves this area intact; exception/service stacks are separate. */
    if (interactions) {
        uniform(gba->memory.iwram + 0x7E00, 0x20, pattern);
        uniform(gba->memory.iwram + 0x7E28, 8, pattern);
        uniform(gba->memory.iwram + 0x7E34, 12, pattern);
        CHECK(word(gba->memory.iwram + 0x7E30) == 0x11224488);
    } else uniform(gba->memory.iwram + 0x7E00, 0x40, pattern);
    uniform(gba->memory.iwram + 0x7E48, 0x78, pattern);
    if (interactions) {
        uniform(gba->memory.iwram + 0x7F00, 0x88, pattern);
        uniform(gba->memory.iwram + 0x7FA0, 0x30, pattern);
        uniform(gba->memory.iwram + 0x7FE0, 0x1C, pattern);
    } else {
        uniform(gba->memory.iwram + 0x7F00, 0xD0, pattern);
        uniform(gba->memory.iwram + 0x7FE0, 0x20, pattern);
    }
    CHECK(cupid_io_read(gba, REG_DISPCNT) == 0x80);
    if ((flags & 0x80) && cupid_io_read(gba, REG_TM0CNT_L) != stoppedCounter) {
        fprintf(stderr, "Reset timer: flags=%08x configuration=%u: %u / %u\n", flags, configuration,
                cupid_io_read(gba, REG_TM0CNT_L), stoppedCounter);
    }
    CHECK(cupid_io_read(gba, REG_TM0CNT_L) == (interactions ? 0xF800 + result->clocks % 2048 :
          flags & 0x80 ? stoppedCounter : (uint16_t)result->clocks));
    CHECK(gba->irqf == (interactions || (flags & 0x80) ? 0 : 0x3FFF));
    CHECK(cupid_io_read(gba, REG_SOUNDBIAS) == (flags & 0x40 ? 0x156 : 0xC156));
    uniform(gba->audio.psg.ch3.wavedata8, 32, flags & 0x40 ? 0 : pattern);
    if (!(flags & 0x20)) CHECK((cupid_io_read(gba, REG_SIODATA32_LO) & 255) == 7);
    memcpy(result->registers, gba->cpu.gprs, sizeof(result->registers));
    result->cpsr = (uint32_t)gba->cpu.cpsr.packed;
    memcpy(result->ewram, gba->memory.ewram, sizeof(result->ewram));
    memcpy(result->iwram, gba->memory.iwram, sizeof(result->iwram));
    /* Stack bytes after return contain implementation-specific scratch values. */
    memset(result->iwram + 0x7EC0, 0, 0x40);
    memset(result->iwram + 0x7FD0, 0, 0x10);
    if (interactions) {
        memset(result->iwram + 0x7F88, 0, 0x18);
        CHECK(word(gba->memory.iwram + 0x7E20) > 1);
        CHECK(word(gba->memory.iwram + 0x7E24) == (IRQ_TIMER0 | IRQ_DMA0));
        CHECK(trace->count >= 5);
        CHECK(trace->values[0][0] == 0x5A5A5A5A && trace->values[0][2] == 0x5A5A5A5A);
        bool sawReplacement = false, sawOverwritten = false;
        for (unsigned i = 0; i < trace->count; i++) {
            if (trace->values[i][1] == 0x11224488) sawReplacement = true;
            if (sawReplacement && trace->values[i][1] == 0) sawOverwritten = true;
        }
        CHECK(sawReplacement && sawOverwritten);
        CHECK(trace->values[trace->count-1][2] == 0);
        trace->gba = NULL;
    }
    memcpy(result->palette, gba->video.palette, sizeof(result->palette));
    memcpy(result->vram, gba->video.vram, sizeof(result->vram));
    memcpy(result->oam, gba->video.oam.raw, sizeof(result->oam));
    memcpy(result->wave, gba->audio.psg.ch3.wavedata8, sizeof(result->wave));
    result->fifo[0] = gba->audio.chA;
    result->fifo[1] = gba->audio.chB;
    for (unsigned i = 0; i < sizeof(result->io) / sizeof(*result->io); i++) result->io[i] = cupid_io_read(gba, (uint16_t)(i * 2));
    cupid_gba_destroy(gba);
    free(gba);
    return result;
}

static uint64_t qualify(uint32_t flags, unsigned configuration) {
    struct ResetTrace builtInTrace, externalTrace;
    struct Result* builtIn = execute(flags, configuration, NULL, &builtInTrace);
    if (externalBios) {
        struct Result* external = execute(flags, configuration, externalBios, &externalTrace);
        if (builtIn->clocks != external->clocks) fprintf(stderr, "Reset clocks: flags=%08x configuration=%u: %llu / %llu\n",
            flags, configuration, (unsigned long long)builtIn->clocks, (unsigned long long)external->clocks);
        CHECK(builtIn->clocks == external->clocks);
        CHECK(!memcmp(builtIn, external, sizeof(*builtIn)));
        if (configuration & 16) {
            CHECK(builtInTrace.count == externalTrace.count);
            CHECK(!memcmp(builtInTrace.times, externalTrace.times, builtInTrace.count * sizeof(builtInTrace.times[0])));
            CHECK(!memcmp(builtInTrace.values, externalTrace.values, builtInTrace.count * sizeof(builtInTrace.values[0])));
        }
        free(external);
    }
    const uint64_t clocks = builtIn->clocks;
    free(builtIn);
    observations++;
    return clocks;
}

static void all_masks_return_state_and_device_effects(void) {
    for (unsigned group = firstGroup; group <= lastGroup; group++) {
        for (unsigned mask = group * 16; mask < (group + 1) * 16; mask++) {
            for (unsigned thumb = 0; thumb < 2; thumb++) qualify(mask, thumb ? 9 : 0);
        }
    }
}

static void upper_bits_and_ram_callers(void) {
    const uint32_t upper[] = {0x100, 0x80000000, 0xFFFFFF00};
    for (unsigned group = firstGroup; group <= lastGroup; group++) {
        if (group & 3) continue;
        for (unsigned high = 0; high < 3; high++) {
            for (unsigned bits = 0; bits < 3; bits++) {
                const uint32_t flags = group * 16 | (bits == 1 ? 31 : bits == 2 ? 63 : 0) | upper[high];
                for (unsigned thumb = 0; thumb < 2; thumb++) qualify(flags, thumb);
            }
        }
        const unsigned masks[] = {0, 4, 16, 32};
        for (unsigned i = 0; i < 4; i++) {
            for (unsigned region = 1; region <= 2; region++) {
                for (unsigned thumb = 0; thumb < 2; thumb++) qualify(group * 16 | masks[i], region * 2 | thumb);
            }
        }
    }
}

int main(int argc, char** argv) {
    for (int argument = 1; argument < argc; argument++) {
        if (!strcmp(argv[argument], "--external-bios") && argument + 1 < argc) externalBios = argv[++argument];
        else if (!strcmp(argv[argument], "--group") && argument + 1 < argc) {
            char* end;
            const unsigned long group = strtoul(argv[++argument], &end, 10);
            if (!argv[argument][0] || *end || group > 15) return 2;
            firstGroup = lastGroup = (unsigned)group;
        } else if (!strcmp(argv[argument], "--interactions")) interactionSelection = true;
        else return 2;
    }
    if (interactionSelection) {
        for (unsigned thumb = 0; thumb < 2; thumb++) qualify(1, 16 | thumb);
        CHECK(observations == 2);
    } else {
        RUN(all_masks_return_state_and_device_effects);
        RUN(upper_bits_and_ram_callers);
        unsigned extra = 0;
        for (unsigned group = firstGroup; group <= lastGroup; group++) if (!(group & 3)) extra += 34;
        CHECK(observations == (lastGroup - firstGroup + 1) * 32 + extra);
    }
    printf("Passed %u register reset observations\n", observations);
    return 0;
}
