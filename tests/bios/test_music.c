/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"
#include "caller.h"

static const char* externalBios;

struct MusicCase { uint32_t base, key, fine, frequency, factor; unsigned clocks; };
static const struct MusicCase musicCases[] = {
    {0x00008000, 0x00000000, 0x00000000, 0x00000001, 0x00020000, 145},
    {0xFFFFFFFF, 0x00000000, 0x000000FF, 0x00021E51, 0x00021E52, 148},
    {0x01000000, 0x0000000B, 0x000000FF, 0x000003FF, 0x0003FFC6, 148},
    {0x01000000, 0x0000000C, 0x00000000, 0x00000400, 0x00040000, 145},
    {0x01000000, 0x00000045, 0x00000000, 0x00006BA2, 0x006BA27E, 145},
    {0x00008000, 0x0000007F, 0x0000007F, 0x0000062B, 0x0C570D67, 149},
    {0x00008000, 0x0000007F, 0x000000FF, 0x00000659, 0x0CB248DB, 149},
    {0x00008000, 0x00000080, 0x00000080, 0x00000689, 0x0D13A78F, 149},
    {0x00123456, 0x000000B2, 0x00000000, 0x001037E3, 0xE411F03A, 146},
    {0x00123456, 0x000000B2, 0x000000FF, 0x00112DCE, 0xF1942F6A, 149},
    {0x00123456, 0x000000B3, 0x00000000, 0x00112DCE, 0xF1942F6A, 151},
    {0x00123456, 0x000000B4, 0x0000007F, 0x00112DCE, 0xF1942F6A, 151},
    {0x00123456, 0x000000FF, 0x00000000, 0x00112DCE, 0xF1942F6A, 151},
    {0xFFFFFFFF, 0x00000080, 0x0000007F, 0x0D12E63D, 0x0D12E63E, 149},
    {0xFFFFFFFF, 0x0000007F, 0x00000080, 0x0C57C3DD, 0x0C57C3DE, 149},
    {0x00123456, 0x0000007F, 0xFFFFFF7F, 0x0000E0A4, 0x0C570D67, 149},
    {0x00123456, 0x0000007F, 0x00000100, 0x0000DA34, 0x0BFC886B, 146},
    {0x00123456, 0x0000007F, 0x00000101, 0x0000DA41, 0x0BFD3EE1, 149},
};

static struct CupidGBA* machine(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    if (externalBios) CHECK(cupid_load_bios(gba, externalBios));
    cupid_gba_reset(gba);
    cupid_test_bios_caller(gba);
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static uint64_t call_from(struct CupidGBA* gba, const struct MusicCase* input, bool thumb,
                          enum CupidPrivMode mode, uint32_t source) {
    const uint32_t wide[] = {0xEF1F0000, 0xEAFFFFFE};
    const uint16_t narrow[] = {0xDF1F, 0xE7FE};
    memcpy(gba->memory.iwram, thumb ? (const void*)narrow : (const void*)wide,
        thumb ? sizeof(narrow) : sizeof(wide));
    memcpy(gba->memory.ewram + 4, &input->base, sizeof(input->base));
    cupid_arm_set_priv_mode(&gba->cpu, mode);
    for (unsigned r = 0; r < 15; r++) gba->cpu.gprs[r] = (int32_t)(0x11220000 + r);
    gba->cpu.gprs[0] = (int32_t)source;
    gba->cpu.gprs[1] = (int32_t)input->key;
    gba->cpu.gprs[2] = (int32_t)input->fine;
    gba->cpu.gprs[ARM_SP] = mode == MODE_IRQ ? GBA_SP_BASE_IRQ : GBA_SP_BASE_SYSTEM;
    gba->cpu.cpsr.packed = 0xA0000000 | mode;
    gba->cpu.spsr.packed = 0x6000003F;
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
    if (thumb) CupidThumbWritePC(&gba->cpu);
    else CupidARMWritePC(&gba->cpu);
    const uint64_t start = cupid_timing_global_time(&gba->timing);
    unsigned steps;
    for (steps = 0; steps < 20000; steps++) {
        cupid_gba_step(gba);
        if (gba->cpu.executionMode == (thumb ? MODE_THUMB : MODE_ARM) &&
            (uint32_t)gba->cpu.gprs[ARM_PC] == GBA_BASE_IWRAM + (thumb ? 4 : 8)) break;
    }
    CHECK(steps < 20000);
    CHECK(gba->cpu.privilegeMode == mode);
    CHECK((uint32_t)gba->cpu.cpsr.packed == (0xA0000000u | mode | (thumb ? 32u : 0u)));
    CHECK((uint32_t)gba->cpu.gprs[2] == input->fine);
    CHECK(gba->cpu.gprs[3] == 0x170);
    for (unsigned r = 4; r < 15; r++) {
        CHECK((uint32_t)gba->cpu.gprs[r] == (r == ARM_SP
            ? (mode == MODE_IRQ ? GBA_SP_BASE_IRQ : GBA_SP_BASE_SYSTEM) : 0x11220000u + r));
    }
    if (mode == MODE_IRQ) CHECK((uint32_t)gba->cpu.spsr.packed == 0x6000003F);
    return cupid_timing_global_time(&gba->timing) - start;
}

static uint64_t call(struct CupidGBA* gba, const struct MusicCase* input, bool thumb, enum CupidPrivMode mode) {
    return call_from(gba, input, thumb, mode, GBA_BASE_EWRAM);
}

static void interpolation_clamps_and_caller_banks(void) {
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        for (unsigned irq = 0; irq < 2; irq++) {
            for (unsigned i = 0; i < sizeof(musicCases) / sizeof(*musicCases); i++) {
                struct CupidGBA* gba = machine();
                const struct MusicCase* expected = &musicCases[i];
                CHECK(call(gba, expected, thumb != 0, irq ? MODE_IRQ : MODE_SYS) == expected->clocks);
                CHECK((uint32_t)gba->cpu.gprs[0] == expected->frequency);
                CHECK((uint32_t)gba->cpu.gprs[1] == expected->factor);
                destroy(gba);
            }
        }
    }
}

static void wave_reads_use_alignment_region_and_wait_controls(void) {
    const uint64_t clocks[] = {0, UINT64_C(0xFFFFFFC0), UINT64_C(0x1FFFFFFC0)};
    const uint32_t frequencies[] = {0x2468, 0xF024, 0xACF0, 0x68AC};
    const uint32_t addresses[] = {GBA_BASE_EWRAM, GBA_BASE_IWRAM, GBA_BASE_ROM0, GBA_BASE_ROM1, GBA_BASE_ROM2};
    const unsigned ordinary[] = {145, 140, 147, 149, 153};
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        for (unsigned fast = 0; fast < 2; fast++) {
            for (unsigned region = 0; region < 5; region++) {
                for (unsigned alignment = 0; alignment < 4; alignment++) {
                    for (unsigned c = 0; c < 3; c++) {
                        struct CupidGBA* gba = machine();
                        cupid_timing_clear(&gba->timing);
                        gba->timing.globalCycles = clocks[c];
                        gba->timing.masterCycles = (uint32_t)clocks[c];
                        gba->memory.rom = calloc(1, 0x2000);
                        CHECK(gba->memory.rom);
                        gba->memory.romSize = 0x2000;
                        gba->memory.romMask = 0x1FFF;
                        uint8_t* bytes = region == 0 ? gba->memory.ewram : region == 1 ? gba->memory.iwram : gba->memory.rom;
                        const uint32_t word = 0x12345678;
                        memcpy(bytes + 0x1000, &word, sizeof(word));
                        cupid_memory_update_waitstates(gba, fast ? 0x0490 : 0);
                        struct MusicCase input = {word, 0, 0, frequencies[alignment], 0x20000, 0};
                        const uint64_t elapsed = call_from(gba, &input, thumb != 0, MODE_SYS,
                            addresses[region] + 0xFFC + alignment);
                        CHECK(elapsed == (fast && region >= 2 ? 146 : ordinary[region]));
                        CHECK((uint32_t)gba->cpu.gprs[0] == input.frequency);
                        CHECK((uint32_t)gba->cpu.gprs[1] == input.factor);
                        CHECK(gba->memory.biosPrefetch == 0xE3A02004);
                        destroy(gba);
                    }
                }
            }
        }
    }
}

struct MusicDMA { struct CupidGBA* gba; bool dispatched; uint64_t when; uint32_t late; };

static void replace_wave(struct CupidTiming* timing, void* context, uint32_t late) {
    struct MusicDMA* trace = context;
    struct CupidGBA* gba = trace->gba;
    CHECK((uint32_t)gba->cpu.gprs[ARM_PC] < GBA_SIZE_BIOS);
    trace->dispatched = true;
    trace->when = cupid_timing_global_time(timing);
    trace->late = late;
    CHECK(timing->globalCycles == trace->when + late);
    cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM + 0x1000);
    cupid_dma_write_dad(gba, 3, GBA_BASE_EWRAM + 4);
    cupid_dma_write_cnt_lo(gba, 3, 1);
    cupid_dma_write_cnt(gba, 3, 0xC400);
}

static uint32_t word(const uint8_t* bytes) {
    uint32_t result;
    memcpy(&result, bytes, sizeof(result));
    return result;
}

struct MusicProgress {
    uint64_t clocks;
    uint32_t registers[16], status, savedStatus, protectedWord;
    uint32_t interrupts, requests, lateness;
    uint16_t timer, pending;
};
static struct MusicProgress builtinProgress[2][2][3];

static void timer_dma_and_irq_execute_during_interpolation(void) {
    if (externalBios) {
        const char* requested = externalBios;
        externalBios = NULL;
        timer_dma_and_irq_execute_during_interpolation();
        externalBios = requested;
    }
    const uint64_t clocks[] = {0, UINT64_C(0xFFFFFFC0), UINT64_C(0x1FFFFFFC0)};
    for (unsigned thumb = 0; thumb < 2; thumb++) {
      for (unsigned phase = 0; phase < 2; phase++) {
        for (unsigned c = 0; c < 3; c++) {
            struct CupidGBA* gba = machine();
            cupid_timing_clear(&gba->timing);
            gba->timing.globalCycles = clocks[c];
            gba->timing.masterCycles = (uint32_t)clocks[c];
            const uint32_t handler[] = {
                0xE3A00301, 0xE2800C02, 0xE1D010B2, 0xE1C010B2,
                0xE59F2018, 0xE5923000, 0xE2833001, 0xE5823000,
                0xE5923004, 0xE1833001, 0xE5823004, 0xE12FFF1E,
                GBA_BASE_EWRAM + 0x2B00
            };
            memcpy(gba->memory.iwram + 0x7000, handler, sizeof(handler));
            const uint32_t entry = GBA_BASE_IWRAM + 0x7000, replacement = 0x123456;
            memcpy(gba->memory.iwram + 0x7FFC, &entry, sizeof(entry));
            memcpy(gba->memory.iwram + 0x1000, &replacement, sizeof(replacement));
            cupid_io_write(gba, REG_TM0CNT_L, 0xFF80);
            cupid_io_write(gba, REG_TM0CNT_H, 0xC0);
            cupid_timing_tick(&gba->timing, 2);
            cupid_io_write(gba, REG_IE, IRQ_TIMER0 | IRQ_DMA3);
            cupid_io_write(gba, REG_IME, 1);
            struct MusicDMA trace = {.gba = gba};
            struct CupidTimingEvent event = {.context = &trace, .callback = replace_wave, .priority = 0x20};
            const unsigned delay = phase ? 84 : 40;
            cupid_timing_schedule(&gba->timing, &event, (int32_t)delay);
            const struct MusicCase input = {32768, 127, 127, 0xE0A4, 0x0C570D67, 149};
            const uint64_t elapsed = call(gba, &input, thumb != 0, MODE_SYS);
            CHECK(trace.dispatched && trace.when == clocks[c] + 2 + delay);
            if (phase) CHECK(trace.late > 0);
            CHECK(elapsed > input.clocks);
            CHECK((uint32_t)gba->cpu.gprs[0] == input.frequency);
            CHECK((uint32_t)gba->cpu.gprs[1] == input.factor);
            CHECK(word(gba->memory.ewram + 4) == replacement);
            CHECK(!gba->memory.dmaRunning && !gba->memory.dma[3].enable);
            CHECK(word(gba->memory.ewram + 0x2B00) > 1);
            CHECK(word(gba->memory.ewram + 0x2B04) == (IRQ_TIMER0 | IRQ_DMA3));
            CHECK(cupid_io_read(gba, REG_TM0CNT_L) == 0xFF80 + elapsed % 128);
            struct MusicProgress result = {0};
            result.clocks = elapsed;
            memcpy(result.registers, gba->cpu.gprs, sizeof(result.registers));
            result.status = (uint32_t)gba->cpu.cpsr.packed;
            result.savedStatus = (uint32_t)gba->cpu.spsr.packed;
            result.protectedWord = gba->memory.biosPrefetch;
            result.interrupts = word(gba->memory.ewram + 0x2B00);
            result.requests = word(gba->memory.ewram + 0x2B04);
            result.lateness = trace.late;
            result.timer = cupid_io_read(gba, REG_TM0CNT_L);
            result.pending = gba->irqf;
            if (externalBios) {
                const struct MusicProgress* expected = &builtinProgress[thumb][phase][c];
                CHECK(result.clocks == expected->clocks);
                CHECK(!memcmp(result.registers, expected->registers, sizeof(result.registers)));
                CHECK(result.status == expected->status && result.savedStatus == expected->savedStatus);
                CHECK(result.protectedWord == expected->protectedWord);
                CHECK(result.interrupts == expected->interrupts && result.requests == expected->requests);
                CHECK(result.lateness == expected->lateness);
                CHECK(result.timer == expected->timer && result.pending == expected->pending);
            } else builtinProgress[thumb][phase][c] = result;
            destroy(gba);
        }
      }
    }
}

int main(int argc, char** argv) {
    if (argc == 3 && !strcmp(argv[1], "--external-bios")) externalBios = argv[2];
    else if (argc != 1) return 2;
    RUN(interpolation_clamps_and_caller_banks);
    RUN(wave_reads_use_alignment_region_and_wait_controls);
    RUN(timer_dma_and_irq_execute_during_interpolation);
    return 0;
}
