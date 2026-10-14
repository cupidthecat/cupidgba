/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

static const char* externalBios;
static uint8_t externalImage[GBA_SIZE_BIOS];
static bool externalImageLoaded;
static unsigned selection, firstByte, lastByte = 255, observations;
static const enum CupidPrivMode bankModes[] = {MODE_SYS, MODE_FIQ, MODE_IRQ, MODE_SVC, MODE_ABT, MODE_UND};
static const enum CupidPrivMode callerModes[] = {MODE_SYS, MODE_USR, MODE_IRQ, MODE_SVC, MODE_ABT, MODE_UND, MODE_FIQ};

struct Result {
    uint64_t clocks;
    uint32_t registers[16], cpsr, banks[6][7], savedStatus[6], pipeline[2];
    uint16_t io[0x210 / 2];
    uint8_t iwram[GBA_SIZE_IWRAM];
};

struct Trace {
    struct CupidGBA* gba;
    struct CupidTimingEvent sample, dma;
    uint64_t times[144];
    uint8_t values[144][512];
    unsigned count;
};

static void sample_clear(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct Trace* trace = context;
    const uint8_t* data = trace->gba->memory.iwram + 0x7E00;
    if (!trace->count || memcmp(trace->values[trace->count-1], data, 512)) {
        CHECK(trace->count < 144);
        memcpy(trace->values[trace->count], data, 512);
        trace->times[trace->count++] = cupid_timing_global_time(timing);
    }
    cupid_timing_schedule(timing, &trace->sample, 1);
}

static void start_dma(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing;
    (void)late;
    cupid_io_write(context, REG_DMA0CNT_HI, 0xC400);
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
    cupid_video_reset(&gba->video);
    cupid_io_write(gba, REG_POSTFLG, 1);
    return gba;
}

/* Configuration: Thumb, caller region, fast ROM, DMA case, caller bank. */
static struct Result* execute(unsigned flag, unsigned configuration, const char* bios, struct Trace* trace) {
    struct CupidGBA* gba = machine(bios);
    const bool thumb = configuration & 1, dma = configuration & 16;
    const unsigned region = (configuration >> 1) & 3, caller = configuration >> 5;
    CHECK(region < 3 && caller < 7);
    uint8_t* expected = malloc(GBA_SIZE_EWRAM + GBA_SIZE_IWRAM);
    CHECK(expected);
    memset(gba->memory.ewram, 0x3C, GBA_SIZE_EWRAM);
    memset(gba->memory.iwram, 0x5A, GBA_SIZE_IWRAM);
    memset(gba->video.palette, 0x5A, GBA_SIZE_PALETTE);
    memset(gba->video.vram, 0x5A, GBA_SIZE_VRAM);
    memset(gba->video.oam.raw, 0x5A, GBA_SIZE_OAM);
    gba->memory.rom = calloc(1, 4096);
    CHECK(gba->memory.rom);
    gba->memory.romSize = 4096;
    gba->memory.romMask = 4095;
    const uint32_t romEntry[] = {0xE3A080A1, 0xEAFFFFFE};
    const uint32_t ramEntry[] = {0xE3A080B2, 0xEAFFFFFE};
    const uint32_t arm[] = {0xEF000000, 0xE3A080CC, 0xEAFFFFFE};
    const uint16_t narrow[] = {0xDF00, 0x20CC, 0xE7FE};
    memcpy(gba->memory.rom, romEntry, sizeof(romEntry));
    memcpy(gba->memory.ewram, ramEntry, sizeof(ramEntry));
    uint8_t* code = (region == 0 ? gba->memory.rom : region == 1 ? gba->memory.ewram : gba->memory.iwram) + 0x100;
    const uint32_t address = (region == 0 ? GBA_BASE_ROM0 : region == 1 ? GBA_BASE_EWRAM : GBA_BASE_IWRAM) + 0x100;
    memcpy(code, thumb ? (const void*)narrow : (const void*)arm, thumb ? sizeof(narrow) : sizeof(arm));
    gba->memory.iwram[0x7FFA] = (uint8_t)flag;
    cupid_io_write(gba, REG_WAITCNT, configuration & 8 ? 0x4317 : 0);
    cupid_io_write(gba, REG_DISPCNT, 0x80);
    cupid_io_write(gba, REG_SOUNDBIAS, 0xC156);
    cupid_io_write(gba, REG_IME, 0);
    const uint32_t stacks[] = {0x03007F00, 0x03007F40, 0x03007FA0, 0x03007FE0, 0x03007F20, 0x03007F60};
    for (unsigned b = 0; b < 6; b++) {
        for (unsigned r = 0; r < 7; r++) gba->cpu.bankedRegisters[b][r] = (int32_t)(0x55660000 + b * 16 + r);
        gba->cpu.bankedRegisters[b][0] = (int32_t)stacks[b];
        gba->cpu.bankedSPSRs[b] = (int32_t)(0x90000000 + b);
    }
    gba->cpu.gprs[ARM_SP] = (int32_t)stacks[0];
    cupid_arm_set_priv_mode(&gba->cpu, callerModes[caller]);
    for (unsigned r = 0; r < 13; r++) gba->cpu.gprs[r] = (int32_t)(0x11220000 + r);
    gba->cpu.gprs[ARM_LR] = 0x12345678;
    gba->cpu.cpsr.packed = (int32_t)(0xA00000C0u | callerModes[caller]);
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = (int32_t)address;
    if (thumb) CupidThumbWritePC(&gba->cpu);
    else CupidARMWritePC(&gba->cpu);
    cupid_io_write(gba, REG_TM0CNT_L, dma ? 0xFFF0 : 0);
    cupid_io_write(gba, REG_TM0CNT_H, dma ? 0xC0 : 0x80);
    cupid_timing_tick(&gba->timing, 2);
    unsigned selected = flag != 0;
    if (dma) {
        CHECK(trace && region == 0 && caller == 0);
        memset(trace, 0, sizeof(*trace));
        trace->gba = gba;
        trace->sample.context = trace;
        trace->sample.callback = sample_clear;
        trace->sample.priority = 0x60;
        cupid_timing_schedule(&gba->timing, &trace->sample, 1);
        const unsigned replacement = flag ? 0 : 1;
        const uint32_t data = replacement << 16;
        memcpy(gba->memory.iwram + 0x7DFC, &data, sizeof(data));
        cupid_io_write(gba, REG_DMA0SAD_LO, 0x7DFC);
        cupid_io_write(gba, REG_DMA0SAD_HI, 0x0300);
        cupid_io_write(gba, REG_DMA0DAD_LO, 0x7FF8);
        cupid_io_write(gba, REG_DMA0DAD_HI, 0x0300);
        cupid_io_write(gba, REG_DMA0CNT_LO, 1);
        trace->dma.context = gba;
        trace->dma.callback = start_dma;
        trace->dma.priority = 0x20;
        const unsigned delay = configuration & 8 ? 64 : 1;
        cupid_timing_schedule(&gba->timing, &trace->dma, delay);
        cupid_io_write(gba, REG_IE, IRQ_TIMER0 | IRQ_DMA0);
        if (delay == 1) selected = replacement;
    }
    memcpy(expected, gba->memory.ewram, GBA_SIZE_EWRAM);
    memcpy(expected + GBA_SIZE_EWRAM, gba->memory.iwram, GBA_SIZE_IWRAM);
    memset(expected + GBA_SIZE_EWRAM + 0x7E00, 0, 512);
    const uint32_t destination = selected ? GBA_BASE_EWRAM : GBA_BASE_ROM0;
    const uint64_t start = cupid_timing_global_time(&gba->timing);
    unsigned steps;
    for (steps = 0; steps < 2000; steps++) {
        cupid_gba_step(gba);
        if (gba->cpu.executionMode == MODE_ARM && gba->cpu.privilegeMode == MODE_SYS &&
            (uint32_t)gba->cpu.gprs[ARM_PC] == destination + 4) break;
    }
    CHECK(steps < 2000);
    struct Result* result = calloc(1, sizeof(*result));
    CHECK(result);
    result->clocks = cupid_timing_global_time(&gba->timing) - start;
    CHECK(result->clocks > 800);
    CHECK(gba->cpu.cpsr.packed == 0x1F);
    for (unsigned r = 0; r < 13; r++) CHECK(gba->cpu.gprs[r] == 0);
    CHECK((uint32_t)gba->cpu.gprs[ARM_SP] == GBA_SP_BASE_SYSTEM);
    CHECK((uint32_t)gba->cpu.gprs[ARM_LR] == destination);
    const uint32_t* entry = selected ? ramEntry : romEntry;
    CHECK(gba->cpu.prefetch[0] == entry[0] && gba->cpu.prefetch[1] == entry[1]);
    CHECK(cupid_memory_load32(&gba->cpu, 0, NULL) == 0xE129F000);
    CHECK(!memcmp(gba->memory.ewram, expected, GBA_SIZE_EWRAM));
    CHECK(!memcmp(gba->memory.iwram, expected + GBA_SIZE_EWRAM, GBA_SIZE_IWRAM));
    free(expected);
    const uint8_t* areas[] = {(const uint8_t*)gba->video.palette, (const uint8_t*)gba->video.vram, (const uint8_t*)gba->video.oam.raw};
    const unsigned sizes[] = {GBA_SIZE_PALETTE, GBA_SIZE_VRAM, GBA_SIZE_OAM};
    for (unsigned a = 0; a < 3; a++) for (unsigned i = 0; i < sizes[a]; i++) CHECK(areas[a][i] == 0x5A);
    CHECK(cupid_io_read(gba, REG_DISPCNT) == 0x80 && cupid_io_read(gba, REG_SOUNDBIAS) == 0xC156);
    CHECK(cupid_io_read(gba, REG_TM0CNT_L) == (dma ? 0xFFF0 + result->clocks % 16 : (uint16_t)result->clocks));
    CHECK(cupid_io_read(gba, REG_TM0CNT_H) == (dma ? 0xC0 : 0x80));
    if (dma) {
        CHECK(gba->irqf == (IRQ_TIMER0 | IRQ_DMA0));
        CHECK(cupid_io_read(gba, REG_IME) == 0);
        CHECK(trace->count >= 128);
        CHECK(!gba->memory.dma[0].enable);
        cupid_timing_deschedule(&gba->timing, &trace->sample);
        trace->gba = NULL;
    } else if (configuration == 0) CHECK(result->clocks == (flag ? 876 : 878));
    memcpy(result->registers, gba->cpu.gprs, sizeof(result->registers));
    result->cpsr = (uint32_t)gba->cpu.cpsr.packed;
    memcpy(result->pipeline, gba->cpu.prefetch, sizeof(result->pipeline));
    memcpy(result->iwram, gba->memory.iwram, sizeof(result->iwram));
    for (unsigned b = 0; b < 6; b++) {
        cupid_arm_set_priv_mode(&gba->cpu, bankModes[b]);
        memcpy(result->banks[b], gba->cpu.gprs + 8, sizeof(result->banks[b]));
        result->savedStatus[b] = b ? (uint32_t)gba->cpu.spsr.packed : (uint32_t)gba->cpu.cpsr.packed;
    }
    cupid_arm_set_priv_mode(&gba->cpu, MODE_SYS);
    CHECK(result->banks[2][5] == GBA_SP_BASE_IRQ && result->banks[2][6] == 0 && result->savedStatus[2] == 0x10);
    CHECK(result->banks[3][5] == GBA_SP_BASE_SUPERVISOR && result->banks[3][6] == 0 && result->savedStatus[3] == 0x10);
    for (unsigned i = 0; i < sizeof(result->io) / sizeof(*result->io); i++) result->io[i] = cupid_io_read(gba, (uint16_t)(i * 2));
    cupid_gba_step(gba);
    CHECK(gba->cpu.gprs[8] == (selected ? 0xB2 : 0xA1));
    cupid_gba_step(gba);
    CHECK(gba->cpu.gprs[8] == (selected ? 0xB2 : 0xA1) && gba->cpu.cpsr.packed == 0x1F);
    cupid_gba_destroy(gba);
    free(gba);
    return result;
}

static void qualify(unsigned flag, unsigned configuration) {
    struct Trace builtInTrace, externalTrace;
    struct Result* builtIn = execute(flag, configuration, NULL, &builtInTrace);
    if (externalBios) {
        struct Result* external = execute(flag, configuration, externalBios, &externalTrace);
        if (memcmp(builtIn, external, sizeof(*builtIn))) fprintf(stderr, "Soft reset mismatch: byte=%02x configuration=%u clocks=%llu/%llu\n",
            flag, configuration, (unsigned long long)builtIn->clocks, (unsigned long long)external->clocks);
        CHECK(!memcmp(builtIn, external, sizeof(*builtIn)));
        if (configuration & 16) {
            CHECK(builtInTrace.count == externalTrace.count);
            CHECK(!memcmp(builtInTrace.times, externalTrace.times, builtInTrace.count * sizeof(builtInTrace.times[0])));
            CHECK(!memcmp(builtInTrace.values, externalTrace.values, builtInTrace.count * sizeof(builtInTrace.values[0])));
        }
        free(external);
    }
    free(builtIn);
    observations++;
}

static void restart_bytes_and_first_entry_instructions(void) {
    for (unsigned byte = firstByte; byte <= lastByte; byte++) for (unsigned thumb = 0; thumb < 2; thumb++) qualify(byte, thumb);
}

static void caller_banks_regions_and_waits(void) {
    const unsigned bytes[] = {0, 2, 255};
    for (unsigned bank = 0; bank < 7; bank++) for (unsigned b = 0; b < 3; b++) {
        for (unsigned region = 0; region < 3; region++) for (unsigned mode = 0; mode < 2; mode++) {
            for (unsigned fast = 0; fast < 2; fast++) qualify(bytes[b], bank << 5 | region << 1 | mode | fast << 3);
        }
    }
}

static void dma_before_and_after_restart_latch(void) {
    for (unsigned thumb = 0; thumb < 2; thumb++) for (unsigned late = 0; late < 2; late++) {
        qualify(late, 16 | thumb | late << 3);
    }
}

int main(int argc, char** argv) {
    for (int a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--external-bios") && a + 1 < argc) externalBios = argv[++a];
        else if (!strcmp(argv[a], "--bytes") && a + 1 < argc) {
            if (argv[a+1][0] < '0' || argv[a+1][0] > '3' || argv[a+1][1]) return 2;
            firstByte = (unsigned)(argv[++a][0] - '0') * 64;
            lastByte = firstByte + 63;
            selection |= 1;
        } else if (!strcmp(argv[a], "--states")) selection |= 2;
        else if (!strcmp(argv[a], "--dma")) selection |= 4;
        else return 2;
    }
    if (!selection) selection = 7;
    if (selection & 1) RUN(restart_bytes_and_first_entry_instructions);
    if (selection & 2) RUN(caller_banks_regions_and_waits);
    if (selection & 4) RUN(dma_before_and_after_restart_latch);
    CHECK(observations == ((selection & 1 ? (lastByte-firstByte+1) * 2 : 0) + (selection & 2 ? 252 : 0) + (selection & 4 ? 4 : 0)));
    printf("Passed %u soft reset observations\n", observations);
    return 0;
}
