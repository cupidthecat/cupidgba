/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

static const char* externalBios;

static struct CupidGBA* machine(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    if (externalBios) CHECK(cupid_load_bios(gba, externalBios));
    cupid_gba_reset(gba);
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static uint64_t call(struct CupidGBA* gba, unsigned swi, bool thumb) {
    uint32_t arm[] = {UINT32_C(0xEF000000) | swi << 16, UINT32_C(0xEAFFFFFE)};
    uint16_t narrow[] = {(uint16_t)(0xDF00 | swi), 0xE7FE};
    if (thumb) memcpy(gba->memory.iwram, narrow, sizeof(narrow));
    else memcpy(gba->memory.iwram, arm, sizeof(arm));
    gba->cpu.cpsr.packed = 0xA000001F;
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
    if (thumb) CupidThumbWritePC(&gba->cpu);
    else CupidARMWritePC(&gba->cpu);
    uint64_t start = cupid_timing_global_time(&gba->timing);
    unsigned steps;
    for (steps = 0; steps < 20000; steps++) {
        cupid_gba_step(gba);
        if (gba->cpu.executionMode == (thumb ? MODE_THUMB : MODE_ARM) &&
            (uint32_t)gba->cpu.gprs[ARM_PC] == GBA_BASE_IWRAM + (thumb ? 4 : 8)) break;
    }
    CHECK(steps < 20000);
    CHECK(gba->cpu.privilegeMode == MODE_SYS);
    CHECK((uint32_t)gba->cpu.cpsr.packed == (thumb ? 0xA000003F : 0xA000001F));
    return cupid_timing_global_time(&gba->timing) - start;
}

static void division_boundaries_and_caller_registers(void) {
    const int32_t inputs[][2] = {
        {0, 1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}, {7, 3}, {-7, 3},
        {7, -3}, {-7, -3}, {INT32_MIN, -1}, {INT32_MIN, 1},
        {INT32_MAX, -1}, {INT32_MIN, INT32_MIN}, {INT32_MAX, INT32_MIN},
        {INT32_MIN, INT32_MAX}, {0x12345678, 0xFF}, {0xFF, 0x12345678},
        {0, 0}, {1, 0}, {-1, 0}
    };
    for (unsigned configuration = 0; configuration < 4; configuration++) {
        for (unsigned i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
            struct CupidGBA* gba = machine();
            int32_t numerator = inputs[i][0], denominator = inputs[i][1];
            int64_t quotient = denominator ? (int64_t)numerator / denominator : (numerator < 0 ? -1 : 1);
            int64_t remainder = denominator ? (int64_t)numerator % denominator : numerator;
            uint32_t magnitude = (uint32_t)(quotient < 0 ? -quotient : quotient);
            for (unsigned r = 2; r <= 14; r++) gba->cpu.gprs[r] = (int32_t)(0x11220000 + r);
            gba->cpu.gprs[ARM_SP] = GBA_SP_BASE_SYSTEM;
            gba->cpu.gprs[0] = configuration & 2 ? denominator : numerator;
            gba->cpu.gprs[1] = configuration & 2 ? numerator : denominator;
            call(gba, configuration & 2 ? 7 : 6, configuration & 1);
            CHECK((uint32_t)gba->cpu.gprs[0] == (uint32_t)quotient);
            CHECK((uint32_t)gba->cpu.gprs[1] == (uint32_t)remainder);
            CHECK((uint32_t)gba->cpu.gprs[3] == magnitude);
            for (unsigned r = 2; r <= 14; r++) {
                if (r == 3) continue;
                CHECK((uint32_t)gba->cpu.gprs[r] == (r == ARM_SP ? GBA_SP_BASE_SYSTEM : 0x11220000 + r));
            }
            destroy(gba);
        }
    }
}

static void square_root_limits(void) {
    const uint32_t inputs[] = {0, 1, 2, 3, 4, 15, 16, 17, 0xFF, 0x100, 0x101,
                              0x12345678, 0x7FFFFFFF, 0x80000000, 0xFFFE0000,
                              0xFFFE0001, 0xFFFFFFFE, 0xFFFFFFFF};
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        for (unsigned i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
            struct CupidGBA* gba = machine();
            uint32_t input = inputs[i];
            gba->cpu.gprs[0] = (int32_t)input;
            call(gba, 8, thumb);
            uint32_t root = (uint32_t)gba->cpu.gprs[0];
            CHECK((uint64_t)root * root <= input);
            CHECK((uint64_t)(root + 1) * (root + 1) > input);
            destroy(gba);
        }
    }
}

static void angle_fixed_point_values(void) {
    const uint32_t atan[][4] = {
        {0, 0, 0, 0xA2F9}, {1, 0, 0, 0xA2F9},
        {0x3FFF, 0x1FFF, 0xFFFFC002, 0x8001},
        {0x4000, 0x2000, 0xFFFFC000, 0x8000},
        {0x8000, 0x16A2, 0xFFFF0000, 0x22D45},
        {0xC000, 0xFFFFC360, 0x1C000, 0x10480},
        {0xFFFF, 0xFFFFA2FE, 8, 0xA2FF},
        {0xFFFF0000, 0x5D07, 0, 0xA2F9},
        {0xFFFF8000, 0xFFFFE95D, 0xFFFF0000, 0x22D45},
        {0xFFFFFFFF, 0xFFFFFFFF, 0, 0xA2F9}
    };
    const int32_t axes[][3] = {
        {0, 0, 0}, {1, 0, 0}, {-1, 0, 0x8000}, {0, 1, 0x4000}, {0, -1, 0xC000},
        {0x4000, 0x4000, 0x2000}, {-0x4000, 0x4000, 0x6000},
        {-0x4000, -0x4000, 0xA000}, {0x4000, -0x4000, 0xE000},
        {0x4000, 0x10000, 0x3605}, {0x4000, -0x10000, 0xC9FC},
        {INT32_MIN, 1, 0x4000}, {1, INT32_MIN, 0x10000}
    };
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        for (unsigned i = 0; i < sizeof(atan) / sizeof(atan[0]); i++) {
            struct CupidGBA* gba = machine();
            gba->cpu.gprs[0] = (int32_t)atan[i][0];
            call(gba, 9, thumb);
            CHECK((uint32_t)gba->cpu.gprs[0] == atan[i][1]);
            CHECK((uint32_t)gba->cpu.gprs[1] == atan[i][2]);
            CHECK((uint32_t)gba->cpu.gprs[3] == atan[i][3]);
            destroy(gba);
        }
        for (unsigned i = 0; i < sizeof(axes) / sizeof(axes[0]); i++) {
            struct CupidGBA* gba = machine();
            gba->cpu.gprs[0] = axes[i][0];
            gba->cpu.gprs[1] = axes[i][1];
            call(gba, 0xA, thumb);
            CHECK(gba->cpu.gprs[0] == axes[i][2]);
            CHECK(gba->cpu.gprs[3] == 0x170);
            destroy(gba);
        }
    }
}

static void copy_fill_and_block_rounding(void) {
    const unsigned counts[] = {0, 1, 7, 8, 9, 16};
    for (unsigned configuration = 0; configuration < 16; configuration++) {
        bool thumb = configuration & 1, fast = configuration & 2;
        bool fill = configuration & 4, word = fast || (configuration & 8);
        unsigned width = word ? 4 : 2;
        for (unsigned i = 0; i < sizeof(counts) / sizeof(counts[0]); i++) {
            struct CupidGBA* gba = machine();
            unsigned count = counts[i], copied = fast ? (count + 7) & ~7u : count;
            uint8_t* source = gba->memory.ewram;
            uint8_t* destination = gba->memory.ewram + 0x100;
            for (unsigned j = 0; j < 128; j++) source[j] = (uint8_t)(j * 13 + 7);
            memset(destination - 4, 0xA5, 132);
            gba->cpu.gprs[0] = GBA_BASE_EWRAM;
            gba->cpu.gprs[1] = GBA_BASE_EWRAM + 0x100;
            gba->cpu.gprs[2] = (int32_t)(count | (fill ? 1 << 24 : 0) | (word ? 1 << 26 : 0));
            call(gba, fast ? 0xC : 0xB, thumb);
            for (unsigned j = 0; j < copied * width; j++) {
                CHECK(destination[j] == source[fill ? j % width : j]);
            }
            for (unsigned j = copied * width; j < 128; j++) CHECK(destination[j] == 0xA5);
            for (unsigned j = 1; j <= 4; j++) CHECK(destination[-(int)j] == 0xA5);
            CHECK((uint32_t)gba->cpu.gprs[ARM_SP] == GBA_SP_BASE_SYSTEM);
            destroy(gba);
        }
    }
}

static void fast_copy_data_register_results(void) {
    const unsigned counts[] = {0, 1, 8, 9, 16};
    for (unsigned fill = 0; fill < 2; fill++) {
        for (unsigned n = 0; n < sizeof(counts) / sizeof(*counts); n++) {
            struct CupidGBA* gba = machine();
            uint8_t* source = gba->memory.ewram;
            for (unsigned i = 0; i < 128; i++) source[i] = (uint8_t)(i * 13 + 7);
            const unsigned copied = (counts[n] + 7) & ~7u;
            const uint32_t control = (fill ? UINT32_C(0x85000000) : UINT32_C(0x84000000)) | counts[n];
            gba->cpu.gprs[0] = GBA_BASE_EWRAM;
            gba->cpu.gprs[1] = GBA_BASE_EWRAM + 0x100;
            gba->cpu.gprs[2] = (int32_t)control;
            gba->cpu.gprs[3] = (int32_t)UINT32_C(0x98765432);
            call(gba, 0x0C, n & 1);
            uint32_t r3 = UINT32_C(0x98765432);
            if (copied) {
                const unsigned offset = fill ? 0 : (copied - 7) * 4;
                r3 = (uint32_t)source[offset] | (uint32_t)source[offset+1] << 8 |
                     (uint32_t)source[offset+2] << 16 | (uint32_t)source[offset+3] << 24;
            }
            CHECK((uint32_t)gba->cpu.gprs[0] == GBA_BASE_EWRAM + (fill ? 0 : copied * 4));
            CHECK((uint32_t)gba->cpu.gprs[1] == GBA_BASE_EWRAM + 0x100 + copied * 4);
            CHECK((uint32_t)gba->cpu.gprs[2] == control);
            CHECK((uint32_t)gba->cpu.gprs[3] == r3);
            destroy(gba);
        }
    }
}

static void copy_source_guards_and_control_masks(void) {
    const uint32_t sources[] = {0, 0x100, 0x01000000, 0x10000000, 0xFFFFFFFC, 0x0FD00000};
    for (unsigned fast = 0; fast < 2; fast++) {
        for (unsigned i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
            struct CupidGBA* gba = machine();
            memset(gba->memory.ewram + 0x100, 0xA5, 128);
            gba->cpu.gprs[0] = (int32_t)sources[i];
            gba->cpu.gprs[1] = GBA_BASE_EWRAM + 0x100;
            /* Bit 20 must participate in the source end-range check. */
            gba->cpu.gprs[2] = (int32_t)(0x04000001 | (i == 5 ? 0x00100000 : 0));
            call(gba, fast ? 0xC : 0xB, i & 1);
            for (unsigned j = 0; j < 128; j++) CHECK(gba->memory.ewram[0x100 + j] == 0xA5);
            destroy(gba);
        }
        struct CupidGBA* gba = machine();
        uint32_t value = 0x89ABCDEF;
        memcpy(gba->memory.ewram, &value, sizeof(value));
        gba->cpu.gprs[0] = GBA_BASE_EWRAM;
        gba->cpu.gprs[1] = GBA_BASE_EWRAM + 0x100;
        gba->cpu.gprs[2] = (int32_t)0x07E00008; /* Fill/word plus unused count bits. */
        call(gba, fast ? 0xC : 0xB, false);
        for (unsigned i = 0; i < 8; i++) {
            uint32_t result;
            memcpy(&result, gba->memory.ewram + 0x100 + i * 4, sizeof(result));
            CHECK(result == value);
        }
        destroy(gba);
    }
}

static void native_firmware_clock_observations(void) {
    /* ARM caller in IWRAM; also checked with the local external BIOS. */
    const uint32_t cases[][5] = {
        {6, 0x12345678, 0xFF, 0, 330}, {6, 0xFF, 0x12345678, 0, 70},
        {8, 0, 0, 0, 99}, {8, 0xFF, 0, 0, 214}, {8, 0x12345678, 0, 0, 1130},
        {9, 0xFF, 0, 0, 99},
        {0xA, 0, 0, 0, 74}, {0xA, 0x4000, 0x4000, 0, 362},
        {0xA, 0x4000, 0x10000, 0, 338},
        {0xA, 0x80000000, 1, 0, 187}, {0xA, 1, 0x80000000, 0, 186},
        {0xB, GBA_BASE_EWRAM, GBA_BASE_EWRAM, 0x100, 3937},
        {0xC, GBA_BASE_EWRAM, GBA_BASE_EWRAM, 0x100, 3390}
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        struct CupidGBA* gba = machine();
        gba->cpu.gprs[0] = (int32_t)cases[i][1];
        gba->cpu.gprs[1] = (int32_t)cases[i][2];
        gba->cpu.gprs[2] = (int32_t)cases[i][3];
        CHECK(call(gba, cases[i][0], false) == cases[i][4]);
        CHECK(cupid_memory_load32(&gba->cpu, 0, NULL) == 0xE3A02004);
        CHECK(cupid_memory_load16(&gba->cpu, 0, NULL) == 0x2004);
        CHECK(cupid_memory_load16(&gba->cpu, 2, NULL) == 0xE3A0);
        CHECK(cupid_memory_load8(&gba->cpu, 3, NULL) == 0xE3);
        destroy(gba);
    }
}

static void copy_alignment_and_forward_overlap(void) {
    for (unsigned configuration = 0; configuration < 8; configuration++) {
        struct CupidGBA* gba = machine();
        bool word = configuration & 1, fill = configuration & 2, fast = configuration & 4;
        unsigned width = word || fast ? 4 : 2, count = fast ? 8 : 7;
        for (unsigned i = 0; i < 128; i++) gba->memory.ewram[i] = (uint8_t)(i * 13 + 7);
        gba->cpu.gprs[0] = GBA_BASE_EWRAM + 1;
        gba->cpu.gprs[1] = GBA_BASE_EWRAM + 0x103;
        gba->cpu.gprs[2] = (int32_t)(count | (fill ? 1 << 24 : 0) | (width == 4 ? 1 << 26 : 0));
        call(gba, fast ? 0xC : 0xB, configuration & 1);
        unsigned destination = width == 4 ? 0x100 : 0x102;
        for (unsigned i = 0; i < count; i++) {
            unsigned source = fill ? 0 : i * width;
            for (unsigned lane = 0; lane < width; lane++) {
                uint8_t expected;
                if (width == 2) expected = lane ? 0 : gba->memory.ewram[source + 1];
                else if (fast && fill) expected = gba->memory.ewram[(lane + 1) & 3];
                else expected = gba->memory.ewram[source + lane];
                CHECK(gba->memory.ewram[destination + i * width + lane] == expected);
            }
        }
        destroy(gba);
    }
    for (unsigned fast = 0; fast < 2; fast++) {
        struct CupidGBA* gba = machine();
        uint32_t expected[24];
        for (unsigned i = 0; i < 24; i++) expected[i] = 0x1000 + i;
        memcpy(gba->memory.ewram, expected, sizeof(expected));
        /* Every block reads its complete source before publishing the writes. */
        unsigned block = fast ? 8 : 1;
        for (unsigned i = 0; i < 16; i += block) {
            uint32_t temporary[8];
            memcpy(temporary, expected + i, block * sizeof(uint32_t));
            memcpy(expected + i + 1, temporary, block * sizeof(uint32_t));
        }
        gba->cpu.gprs[0] = GBA_BASE_EWRAM;
        gba->cpu.gprs[1] = GBA_BASE_EWRAM + 4;
        gba->cpu.gprs[2] = 0x04000010;
        call(gba, fast ? 0xC : 0xB, false);
        CHECK(!memcmp(gba->memory.ewram, expected, sizeof(expected)));
        destroy(gba);
    }
}

static void division_by_zero_keeps_scheduled_events_running(void) {
    for (unsigned configuration = 0; configuration < 4; configuration++) {
        struct CupidGBA* gba = machine();
        bool thumb = configuration & 1;
        uint32_t arm[] = {0xEF060000, 0xEAFFFFFE};
        uint16_t narrow[] = {0xDF06, 0xE7FE};
        if (thumb) memcpy(gba->memory.iwram, narrow, sizeof(narrow));
        else memcpy(gba->memory.iwram, arm, sizeof(arm));
        gba->cpu.gprs[0] = configuration & 2 ? -2 : 2;
        gba->cpu.gprs[1] = 0;
        _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
        gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
        if (thumb) CupidThumbWritePC(&gba->cpu);
        else CupidARMWritePC(&gba->cpu);
        cupid_io_write(gba, REG_TM0CNT_L, 0xFF00);
        cupid_io_write(gba, REG_TM0CNT_H, 0xC0);
        for (unsigned i = 0; i < 500; i++) cupid_gba_step(gba);
        CHECK((uint32_t)gba->cpu.gprs[ARM_PC] < GBA_SIZE_BIOS);
        CHECK(gba->cpu.executionMode == MODE_ARM);
        CHECK(gba->cpu.gprs[0] == 2 && gba->cpu.gprs[1] == 0);
        CHECK(cupid_timing_global_time(&gba->timing) > 256);
        CHECK(cupid_io_read(gba, REG_IF) == IRQ_TIMER0);
        destroy(gba);
    }
}

struct CopyEvent {
    struct CupidGBA* gba;
    unsigned observedWords;
};

static void interrupt_copy(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing;
    (void)late;
    struct CopyEvent* observation = context;
    struct CupidGBA* gba = observation->gba;
    CHECK((uint32_t)gba->cpu.gprs[ARM_PC] < GBA_SIZE_BIOS);
    uint32_t* destination = (uint32_t*)(void*)(gba->memory.ewram + 0x1000);
    while (observation->observedWords < 64 && destination[observation->observedWords] != 0) {
        CHECK(destination[observation->observedWords] == 0x1000 + observation->observedWords);
        observation->observedWords++;
    }
    CHECK(observation->observedWords > 0 && observation->observedWords < 64);
    for (unsigned i = observation->observedWords; i < 64; i++) CHECK(destination[i] == 0);
    cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM + 0x1000);
    cupid_dma_write_dad(gba, 3, GBA_BASE_EWRAM + 63 * 4);
    cupid_dma_write_cnt_lo(gba, 3, 1);
    cupid_dma_write_cnt(gba, 3, 0xC400);
    cupid_io_write(gba, REG_TM0CNT_L, 0xFF00);
    cupid_io_write(gba, REG_TM0CNT_H, 0xC0);
}

static void copy_progress_dma_and_irq(void) {
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine();
        for (unsigned i = 0; i < 64; i++) {
            uint32_t value = 0x1000 + i;
            memcpy(gba->memory.ewram + i * 4, &value, sizeof(value));
        }
        uint32_t replacement = 0x89ABCDEF;
        memcpy(gba->memory.iwram + 0x1000, &replacement, sizeof(replacement));
        const uint32_t handler[] = {
            0xE3A00301, 0xE2800C02, 0xE1D010B2, 0xE1C010B2,
            0xE2400C01, 0xE3A01000, 0xE1C010B2, 0xE59F000C,
            0xE5901000, 0xE2811001, 0xE5801000, 0xE12FFF1E,
            GBA_BASE_IWRAM + 0x5000
        };
        memcpy(gba->memory.iwram + 0x200, handler, sizeof(handler));
        uint32_t entry = GBA_BASE_IWRAM + 0x200;
        memcpy(gba->memory.iwram + 0x7FFC, &entry, sizeof(entry));
        cupid_io_write(gba, REG_IE, IRQ_TIMER0);
        cupid_io_write(gba, REG_IME, 1);
        struct CopyEvent observation = {gba, 0};
        struct CupidTimingEvent event = {0};
        event.context = &observation;
        event.callback = interrupt_copy;
        event.name = "observe BIOS copy progress";
        cupid_timing_schedule(&gba->timing, &event, 200);
        gba->cpu.gprs[0] = GBA_BASE_EWRAM;
        gba->cpu.gprs[1] = GBA_BASE_EWRAM + 0x1000;
        gba->cpu.gprs[2] = 64;
        call(gba, 0xC, thumb);
        CHECK(observation.observedWords > 0);
        for (unsigned i = 0; i < 64; i++) {
            uint32_t value;
            memcpy(&value, gba->memory.ewram + 0x1000 + i * 4, sizeof(value));
            CHECK(value == (i == 63 ? replacement : 0x1000 + i));
        }
        uint32_t interrupts;
        memcpy(&interrupts, gba->memory.iwram + 0x5000, sizeof(interrupts));
        CHECK(interrupts == 1);
        CHECK(!gba->memory.dmaRunning && !gba->memory.dma[3].enable);
        /* The handler acknowledges every sampled IF bit, including DMA3. */
        CHECK(cupid_io_read(gba, REG_IF) == 0);
        CHECK((uint32_t)gba->cpu.gprs[2] == 64);
        destroy(gba);
    }
}

int main(int argc, char** argv) {
    if (argc == 3 && !strcmp(argv[1], "--external-bios")) externalBios = argv[2];
    else if (argc != 1) return 2;
    RUN(division_boundaries_and_caller_registers);
    RUN(square_root_limits);
    RUN(angle_fixed_point_values);
    RUN(copy_fill_and_block_rounding);
    RUN(fast_copy_data_register_results);
    RUN(copy_source_guards_and_control_masks);
    RUN(native_firmware_clock_observations);
    RUN(copy_alignment_and_forward_overlap);
    RUN(division_by_zero_keeps_scheduled_events_running);
    RUN(copy_progress_dma_and_irq);
    return 0;
}
