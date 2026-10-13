/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"
#include <math.h>

static const char* externalBios;
static uint8_t externalImage[GBA_SIZE_BIOS];
static bool externalImageLoaded;
static unsigned observations;
static unsigned firstCall = 0x0E, lastCall = 0x0F;

struct Entry {
    int16_t xScale, yScale, xCenter, yCenter;
    uint16_t angle;
    uint32_t xOrigin, yOrigin;
};

struct Fixture {
    unsigned call;
    uint32_t count, stride;
    struct Entry entries[256];
};

struct Result {
    uint64_t clocks;
    uint32_t registers[15], cpsr;
    uint16_t timer, requests;
};

struct AffineTrace {
    struct CupidGBA* gba;
    struct CupidTimingEvent sample, dma;
    uint8_t* output;
    unsigned width, count;
    uint64_t times[128];
    uint8_t values[128][272];
};

static uint32_t word(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void observe_output(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct AffineTrace* trace = context;
    if (!trace->count || memcmp(trace->values[trace->count-1], trace->output, trace->width)) {
        CHECK(trace->count < 128);
        memcpy(trace->values[trace->count], trace->output, trace->width);
        trace->times[trace->count++] = cupid_timing_global_time(timing);
    }
    cupid_timing_schedule(timing, &trace->sample, 1);
}

static void start_dma(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing;
    (void)late;
    cupid_io_write(context, REG_DMA0CNT_HI, 0xC400);
}

static void put16(uint8_t* p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t* p, uint32_t value) {
    put16(p, (uint16_t)value);
    put16(p + 2, (uint16_t)(value >> 16));
}

static void encode_entry(uint8_t* data, const struct Entry* e, bool background) {
    if (background) {
        put32(data, e->xOrigin);
        put32(data + 4, e->yOrigin);
        put16(data + 8, (uint16_t)e->xCenter);
        put16(data + 10, (uint16_t)e->yCenter);
        data += 12;
    }
    put16(data, (uint16_t)e->xScale);
    put16(data + 2, (uint16_t)e->yScale);
    put16(data + 4, e->angle);
    put16(data + 6, 0xA5C3);
}

static int32_t shifted_product(int16_t scale, int32_t factor) {
    const int32_t product = scale * factor;
    return product / 16384 - (product < 0 && product % 16384 != 0);
}

/* The numeric model uses independent host arithmetic; guest execution uses the table. */
static int32_t sine(unsigned angle) {
    angle &= 255;
    if (!(angle & 63)) {
        const int32_t cardinal[] = {0, 16384, 0, -16384};
        return cardinal[angle >> 6];
    }
    return (int32_t)(sin(angle * (3.14159265358979323846 / 128.0)) * 16384.0);
}

static void matrix(const struct Entry* e, int32_t result[4]) {
    const int32_t s = sine(e->angle >> 8), c = sine((e->angle >> 8) + 64);
    result[0] = shifted_product(e->xScale, c);
    result[1] = -shifted_product(e->xScale, s);
    result[2] = shifted_product(e->yScale, s);
    result[3] = shifted_product(e->yScale, c);
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
            /* Each fresh core gets the same image without reopening the input file. */
            memcpy(gba->memory.bios, externalImage, sizeof(externalImage));
            gba->memory.fullBios = 1;
            gba->memory.biosLoaded = true;
            gba->hasBios = true;
            gba->biosHLE = false;
        }
    }
    cupid_gba_reset(gba);
    for (unsigned r = 0; r < 15; r++) gba->cpu.gprs[r] = (int32_t)(0x11220000 + r);
    gba->cpu.gprs[ARM_SP] = GBA_SP_BASE_SYSTEM;
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static uint64_t invoke(struct CupidGBA* gba, unsigned call, bool thumb) {
    const uint32_t arm[] = {UINT32_C(0xEF000000) | call << 16, UINT32_C(0xEAFFFFFE)};
    const uint16_t narrow[] = {(uint16_t)(0xDF00 | call), 0xE7FE};
    memcpy(gba->memory.iwram, thumb ? (const void*)narrow : (const void*)arm,
           thumb ? sizeof(narrow) : sizeof(arm));
    gba->cpu.cpsr.packed = 0xA000001F;
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
    if (thumb) CupidThumbWritePC(&gba->cpu);
    else CupidARMWritePC(&gba->cpu);
    const uint64_t start = cupid_timing_global_time(&gba->timing);
    unsigned steps;
    for (steps = 0; steps < 100000; steps++) {
        cupid_gba_step(gba);
        if (gba->cpu.executionMode == (thumb ? MODE_THUMB : MODE_ARM) &&
            (uint32_t)gba->cpu.gprs[ARM_PC] == GBA_BASE_IWRAM + (thumb ? 4 : 8)) break;
    }
    CHECK(steps < 100000);
    CHECK(gba->cpu.privilegeMode == MODE_SYS);
    CHECK((uint32_t)gba->cpu.cpsr.packed == (thumb ? 0xA000003F : 0xA000001F));
    for (unsigned r = 4; r < 15; r++) {
        CHECK((uint32_t)gba->cpu.gprs[r] == (r == ARM_SP ? GBA_SP_BASE_SYSTEM : 0x11220000 + r));
    }
    return cupid_timing_global_time(&gba->timing) - start;
}

/* Configuration: Thumb, IWRAM/ROM source, EWRAM/VRAM/OAM destination, fast ROM. */
static struct Result execute(const struct Fixture* f, unsigned configuration, const char* bios,
                             struct AffineTrace* trace) {
    const unsigned count = f->count <= INT32_MAX ? f->count : 0;
    CHECK(count <= 256);
    const bool thumb = configuration & 1;
    const bool interactions = configuration & 64;
    const unsigned sourceRegion = (configuration >> 1) & 3;
    const unsigned destinationRegion = (configuration >> 3) & 3;
    struct CupidGBA* gba = machine(bios);
    uint8_t* source;
    uint32_t sourceAddress;
    if (sourceRegion == 2) {
        gba->memory.rom = calloc(1, 0x4000);
        CHECK(gba->memory.rom);
        gba->memory.romSize = 0x4000;
        gba->memory.romMask = 0x3FFF;
        source = gba->memory.rom + 0x100;
        sourceAddress = GBA_BASE_ROM0 + 0x100;
        cupid_io_write(gba, REG_WAITCNT, configuration & 32 ? 0x4317 : 0);
    } else {
        CHECK(sourceRegion < 2);
        source = (sourceRegion ? gba->memory.iwram : gba->memory.ewram) + 0x100;
        sourceAddress = (sourceRegion ? GBA_BASE_IWRAM : GBA_BASE_EWRAM) + 0x100;
    }
    const unsigned entrySize = f->call == 0x0E ? 20 : 8;
    uint8_t input[5120] = {0}, expected[12288];
    for (unsigned i = 0; i < count; i++) {
        encode_entry(input + i * entrySize, &f->entries[i], f->call == 0x0E);
    }
    memcpy(source, input, sizeof(input));
    uint8_t* output;
    uint32_t outputAddress;
    unsigned outputSize = sizeof(expected), offset = 1024;
    switch (destinationRegion) {
    case 0: output = gba->memory.iwram + 0x2C00; outputAddress = GBA_BASE_IWRAM + 0x2C00; break;
    case 1: output = gba->memory.ewram + 0x2C00; outputAddress = GBA_BASE_EWRAM + 0x2C00; break;
    case 2: output = (uint8_t*)gba->video.vram + 0x2C00; outputAddress = GBA_BASE_VRAM + 0x2C00; break;
    default: output = (uint8_t*)&gba->video.oam; outputAddress = GBA_BASE_OAM; outputSize = 1024; offset = 128; break;
    }
    memset(output, 0xA5, outputSize);
    memset(expected, 0xA5, sizeof(expected));
    int32_t last[4] = {0};
    uint32_t cursor = offset;
    const struct Entry replacement = {-77, 1234, -4567, 5678, 0xC5FF, 0x12345678, 0x87654321};
    for (unsigned i = 0; i < count; i++) {
        const struct Entry* e = interactions && i == 7 ? &replacement : &f->entries[i];
        matrix(e, last);
        if (f->call == 0x0E) {
            CHECK(cursor + 16 <= outputSize);
            for (unsigned p = 0; p < 4; p++) put16(expected + cursor + 2 * p, (uint16_t)last[p]);
            const uint32_t x = e->xOrigin - (uint32_t)last[0] * (uint32_t)(int32_t)e->xCenter -
                (uint32_t)last[1] * (uint32_t)(int32_t)e->yCenter;
            const uint32_t y = e->yOrigin - (uint32_t)last[2] * (uint32_t)(int32_t)e->xCenter -
                (uint32_t)last[3] * (uint32_t)(int32_t)e->yCenter;
            put32(expected + cursor + 8, x);
            put32(expected + cursor + 12, y);
            cursor += 16;
        } else {
            for (unsigned p = 0; p < 4; p++) {
                CHECK((cursor & ~1u) + 2 <= outputSize);
                put16(expected + (cursor & ~1u), (uint16_t)last[p]);
                cursor += f->stride;
            }
        }
    }
    gba->cpu.gprs[0] = (int32_t)sourceAddress;
    gba->cpu.gprs[1] = (int32_t)(outputAddress + offset);
    gba->cpu.gprs[2] = (int32_t)f->count;
    gba->cpu.gprs[3] = (int32_t)f->stride;
    cupid_io_write(gba, REG_TM0CNT_L, interactions ? 0xFE00 : 0);
    cupid_io_write(gba, REG_TM0CNT_H, interactions ? 0xC0 : 0x80);
    cupid_timing_tick(&gba->timing, 2);
    if (interactions) {
        CHECK(trace && count == 8 && destinationRegion == 1 && sourceRegion == 0);
        memset(trace, 0, sizeof(*trace));
        trace->gba = gba;
        trace->output = output + offset - 4;
        trace->width = f->call == 0x0E ? count * 16 + 8 : count * 4 * f->stride + 8;
        CHECK(trace->width <= sizeof(trace->values[0]));
        trace->sample.context = trace;
        trace->sample.callback = observe_output;
        trace->sample.priority = 0x60;
        cupid_timing_schedule(&gba->timing, &trace->sample, 1);
        const uint32_t handler[] = {
            0xE3A00301, 0xE2800C02, 0xE1D010B2, 0xE1C010B2,
            0xE59F2018, 0xE5923000, 0xE2833001, 0xE5823000,
            0xE5923004, 0xE1833001, 0xE5823004, 0xE12FFF1E,
            GBA_BASE_EWRAM + 0x2B00
        };
        memcpy(gba->memory.iwram + 0x7000, handler, sizeof(handler));
        const uint32_t entry = GBA_BASE_IWRAM + 0x7000;
        memcpy(gba->memory.iwram + 0x7FFC, &entry, sizeof(entry));
        for (unsigned i = 0; i < 64; i++) gba->memory.ewram[0x2A00+i] = (uint8_t)(i * 77 + 0x81);
        encode_entry(gba->memory.ewram + 0x2A00, &replacement, f->call == 0x0E);
        cupid_io_write(gba, REG_DMA0SAD_LO, 0x2A00);
        cupid_io_write(gba, REG_DMA0SAD_HI, 0x0200);
        const uint32_t target = sourceAddress + 7 * entrySize;
        cupid_io_write(gba, REG_DMA0DAD_LO, (uint16_t)target);
        cupid_io_write(gba, REG_DMA0DAD_HI, (uint16_t)(target >> 16));
        cupid_io_write(gba, REG_DMA0CNT_LO, 16);
        trace->dma.context = gba;
        trace->dma.callback = start_dma;
        trace->dma.priority = 0x20;
        cupid_timing_schedule(&gba->timing, &trace->dma, 100);
        cupid_io_write(gba, REG_IE, IRQ_TIMER0 | IRQ_DMA0);
        cupid_io_write(gba, REG_IME, 1);
    }
    struct Result result = {0};
    result.clocks = invoke(gba, f->call, thumb);
    CHECK(result.clocks > 40);
    if (memcmp(output, expected, outputSize)) {
        fprintf(stderr, "Affine output mismatch: call=%x count=%u configuration=%u stride=%08x\n",
                f->call, count, configuration, f->stride);
    }
    CHECK(!memcmp(output, expected, outputSize));
    if (interactions) memcpy(input + 7 * entrySize, gba->memory.ewram + 0x2A00, 64);
    CHECK(!memcmp(source, input, sizeof(input)));
    CHECK((uint32_t)gba->cpu.gprs[0] == sourceAddress + count * entrySize);
    CHECK((uint32_t)gba->cpu.gprs[1] == outputAddress + cursor);
    CHECK((uint32_t)gba->cpu.gprs[2] == f->count);
    CHECK((uint32_t)gba->cpu.gprs[3] == (f->call == 0x0E && count ? (uint32_t)last[0] : f->stride));
    result.timer = cupid_io_read(gba, REG_TM0CNT_L);
    CHECK(result.timer == (interactions ? 0xFE00 + result.clocks % 512 : (uint16_t)result.clocks));
    if (interactions) {
        CHECK(trace->count > 8);
        CHECK(!memcmp(gba->memory.ewram + 0x2A00, source + 7 * entrySize, 64));
        CHECK(word(gba->memory.ewram + 0x2B00) > 1);
        CHECK(word(gba->memory.ewram + 0x2B04) == (IRQ_TIMER0 | IRQ_DMA0));
        CHECK(gba->irqf == 0);
        trace->gba = NULL;
        trace->output = NULL;
    }
    result.requests = gba->irqf;
    result.cpsr = (uint32_t)gba->cpu.cpsr.packed;
    memcpy(result.registers, gba->cpu.gprs, sizeof(result.registers));
    destroy(gba);
    return result;
}

static uint64_t qualify(const struct Fixture* f, unsigned configuration) {
    struct AffineTrace builtInTrace, externalTrace;
    const struct Result builtIn = execute(f, configuration, NULL, &builtInTrace);
    if (externalBios) {
        const struct Result external = execute(f, configuration, externalBios, &externalTrace);
        if (builtIn.clocks != external.clocks) {
            fprintf(stderr, "Affine clocks mismatch: call=%x count=%u configuration=%u: %llu / %llu\n",
                    f->call, f->count, configuration,
                    (unsigned long long)builtIn.clocks, (unsigned long long)external.clocks);
        }
        CHECK(builtIn.clocks == external.clocks);
        CHECK(!memcmp(builtIn.registers, external.registers, sizeof(builtIn.registers)));
        CHECK(builtIn.cpsr == external.cpsr && builtIn.timer == external.timer);
        CHECK(builtIn.requests == external.requests);
        if (configuration & 64) {
            CHECK(builtInTrace.count == externalTrace.count && builtInTrace.width == externalTrace.width);
            CHECK(!memcmp(builtInTrace.times, externalTrace.times, builtInTrace.count * sizeof(builtInTrace.times[0])));
            CHECK(!memcmp(builtInTrace.values, externalTrace.values, builtInTrace.count * sizeof(builtInTrace.values[0])));
        }
    }
    observations++;
    return builtIn.clocks;
}

static void signed_rounding_origins_and_caller_state(void) {
    struct Fixture f = {0};
    f.count = 1;
    f.stride = 2;
    f.entries[0] = (struct Entry){257, -513, 12345, -32767, 0x1980, 0x7FFFFF80, 0x8000007B};
    const int32_t expected[] = {210, -147, -296, -420};
    int32_t actual[4];
    matrix(&f.entries[0], actual);
    CHECK(!memcmp(actual, expected, sizeof(actual)));
    for (f.call = firstCall; f.call <= lastCall; f.call++) {
        CHECK(qualify(&f, 0) == (f.call == 0x0E ? 179 : 124));
        qualify(&f, 1);
    }
}

static void complete_angle_range(void) {
    struct Fixture f = {0};
    f.count = 256;
    f.stride = 2;
    for (unsigned high = 0; high < 256; high++) {
        for (unsigned low = 0; low < 256; low++) {
            const unsigned angle = high * 256 + low;
            f.entries[low] = (struct Entry){(int16_t)(angle * 257 + 73), (int16_t)(~(angle * 131)),
                (int16_t)(angle ^ 0xA5A5), (int16_t)(angle * 251 + 0x1800), (uint16_t)angle,
                UINT32_C(0x7FFFFF80) + angle * 997, UINT32_C(0x8000007B) - angle * 12347};
        }
        const unsigned configuration = (high & 1) | (high % 3) << 1 | (high % 3) << 3 |
            (high & 2 ? 32 : 0);
        for (f.call = firstCall; f.call <= lastCall; f.call++) qualify(&f, configuration);
    }
}

static void memory_counts_and_object_strides(void) {
    const uint32_t counts[] = {0, 1, 3, UINT32_MAX, UINT32_C(0x80000000)};
    const uint32_t strides[] = {0, 1, 2, 3, 8, (uint32_t)-2, (uint32_t)-8};
    struct Fixture f = {0};
    f.entries[0] = (struct Entry){INT16_MIN, INT16_MAX, INT16_MIN, INT16_MAX, 0x40FF, 0, UINT32_MAX};
    f.entries[1] = (struct Entry){-1, 1, -1, 1, 0x8000, UINT32_MAX, 0};
    f.entries[2] = (struct Entry){0, 0, 123, -456, 0xFFFF, 0x12345678, 0xFEDCBA98};
    for (unsigned source = 0; source < 3; source++) {
        for (unsigned destination = 0; destination < 4; destination++) {
            for (unsigned thumb = 0; thumb < 2; thumb++) {
                for (unsigned c = 0; c < sizeof(counts) / sizeof(*counts); c++) {
                    f.count = counts[c];
                    for (f.call = firstCall; f.call <= lastCall; f.call++) {
                        const unsigned limit = f.call == 0x0E ? 1 : sizeof(strides) / sizeof(*strides);
                        for (unsigned s = 0; s < limit; s++) {
                            f.stride = strides[s];
                            qualify(&f, thumb | source << 1 | destination << 3);
                        }
                    }
                }
            }
        }
    }
}

static void output_bus_phases_timer_irq_and_dma(void) {
    struct Fixture f = {0};
    f.count = 8;
    f.stride = 8;
    for (unsigned i = 0; i < 8; i++) {
        f.entries[i] = (struct Entry){(int16_t)(257 + i * 131), (int16_t)(-513 - i * 257),
            (int16_t)(12345 + i * 1024), (int16_t)(-32767 + i * 777), (uint16_t)(0x1980 + i * 817),
            UINT32_C(0x7FFFFF80) + i * 997, UINT32_C(0x8000007B) - i * 12347};
    }
    for (f.call = firstCall; f.call <= lastCall; f.call++) {
        for (unsigned thumb = 0; thumb < 2; thumb++) {
            const uint64_t uninterrupted = qualify(&f, 8 | thumb);
            CHECK(qualify(&f, 72 | thumb) > uninterrupted);
        }
    }
}

int main(int argc, char** argv) {
    for (int argument = 1; argument < argc; argument++) {
        if (!strcmp(argv[argument], "--external-bios") && argument + 1 < argc) externalBios = argv[++argument];
        else if (!strcmp(argv[argument], "--background") && firstCall != lastCall) firstCall = lastCall = 0x0E;
        else if (!strcmp(argv[argument], "--object") && firstCall != lastCall) firstCall = lastCall = 0x0F;
        else return 2;
    }
    RUN(signed_rounding_origins_and_caller_state);
    RUN(complete_angle_range);
    RUN(memory_counts_and_object_strides);
    RUN(output_bus_phases_timer_irq_and_dma);
    CHECK(observations == (firstCall == lastCall ? (firstCall == 0x0E ? 382u : 1102u) : 1484u));
    printf("Passed %u affine observations\n", observations);
    return 0;
}
