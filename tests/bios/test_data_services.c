/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

static const char* externalBios;
static unsigned observations;
static unsigned dataSelection = 7;

struct Fixture {
    unsigned call, inputSize, outputSize, consumed;
    uint8_t input[8192], output[8192], info[8];
    uint32_t r3;
};

struct Result {
    uint64_t clocks;
    uint32_t registers[15], cpsr;
    uint16_t timer, requests;
};

static struct CupidGBA* machine(const char* bios) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    if (bios) CHECK(cupid_load_bios(gba, bios));
    cupid_gba_reset(gba);
    for (unsigned r = 0; r < 15; r++) gba->cpu.gprs[r] = (int32_t)(0x11220000 + r);
    gba->cpu.gprs[ARM_SP] = GBA_SP_BASE_SYSTEM;
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static uint64_t invoke(struct CupidGBA* gba, unsigned swi, bool thumb) {
    const uint32_t arm[] = {UINT32_C(0xEF000000) | swi << 16, UINT32_C(0xEAFFFFFE)};
    const uint16_t narrow[] = {(uint16_t)(0xDF00 | swi), 0xE7FE};
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

static uint32_t word(const uint8_t* data) {
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
           (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

static void header(struct Fixture* f, unsigned type, unsigned size) {
    f->input[0] = (uint8_t)type;
    f->input[1] = (uint8_t)size;
    f->input[2] = (uint8_t)(size >> 8);
    f->input[3] = (uint8_t)(size >> 16);
    f->inputSize = 4;
    f->call = type;
}

static struct Result execute(const struct Fixture* f, unsigned configuration, const char* bios) {
    struct CupidGBA* gba = machine(bios);
    const bool thumb = configuration & 1, internalSource = configuration & 2;
    const bool cartridgeSource = configuration & 16;
    const unsigned destination = (configuration >> 2) & 3;
    if (cartridgeSource) {
        gba->memory.rom = calloc(1, 0x4000);
        CHECK(gba->memory.rom);
        gba->memory.romSize = 0x4000;
        gba->memory.romMask = 0x3FFF;
        cupid_io_write(gba, REG_WAITCNT, configuration & 32 ? 0x4317 : 0);
    }
    const uint32_t sourceAddress = (cartridgeSource ? GBA_BASE_ROM0 :
        internalSource ? GBA_BASE_IWRAM : GBA_BASE_EWRAM) + 0x100;
    const uint32_t destinationAddress = destination == 2 ? GBA_BASE_VRAM + 0x1000 :
        (destination ? GBA_BASE_EWRAM : GBA_BASE_IWRAM) + 0x3000;
    uint8_t* source = (cartridgeSource ? gba->memory.rom :
        internalSource ? gba->memory.iwram : gba->memory.ewram) + 0x100;
    uint8_t* output = destination == 2 ? (uint8_t*)gba->video.vram + 0x1000 :
        (destination ? gba->memory.ewram : gba->memory.iwram) + 0x3000;
    memcpy(source, f->input, f->inputSize);
    memset(output - 4, 0xA5, f->outputSize + 8);
    memcpy(gba->memory.ewram + 0x2F00, f->info, sizeof(f->info));
    gba->cpu.gprs[0] = (int32_t)sourceAddress;
    gba->cpu.gprs[1] = (int32_t)destinationAddress;
    gba->cpu.gprs[2] = GBA_BASE_EWRAM + 0x2F00;
    cupid_io_write(gba, REG_TM0CNT_L, 0);
    cupid_io_write(gba, REG_TM0CNT_H, 0x80);
    cupid_timing_tick(&gba->timing, 2);
    struct Result result = {0};
    result.clocks = invoke(gba, f->call, thumb);
    CHECK(result.clocks > 40);
    CHECK(!memcmp(output, f->output, f->outputSize));
    for (unsigned i = 0; i < 4; i++) CHECK(output[(int)i - 4] == 0xA5 && output[f->outputSize + i] == 0xA5);
    CHECK(!memcmp(source, f->input, f->inputSize));
    CHECK((uint32_t)gba->cpu.gprs[0] == sourceAddress + f->consumed);
    CHECK((uint32_t)gba->cpu.gprs[1] == destinationAddress + f->outputSize);
    CHECK((uint32_t)gba->cpu.gprs[2] == GBA_BASE_EWRAM + 0x2F00);
    CHECK((uint32_t)gba->cpu.gprs[3] == f->r3);
    result.timer = cupid_io_read(gba, REG_TM0CNT_L);
    CHECK(result.timer == (uint16_t)result.clocks);
    result.requests = gba->irqf;
    result.cpsr = (uint32_t)gba->cpu.cpsr.packed;
    memcpy(result.registers, gba->cpu.gprs, sizeof(result.registers));
    destroy(gba);
    return result;
}

static uint64_t qualify_one(const struct Fixture* f, unsigned configuration) {
    const struct Result builtIn = execute(f, configuration, NULL);
    if (externalBios) {
        const struct Result external = execute(f, configuration, externalBios);
        CHECK(builtIn.clocks == external.clocks);
        CHECK(!memcmp(builtIn.registers, external.registers, sizeof(builtIn.registers)));
        CHECK(builtIn.cpsr == external.cpsr && builtIn.timer == external.timer);
        CHECK(builtIn.requests == external.requests);
    }
    observations++;
    return builtIn.clocks;
}

static uint64_t qualify(const struct Fixture* f, unsigned configuration) {
    const unsigned source = configuration & 2 ? 2 : 1;
    const uint64_t clocks = dataSelection & source ? qualify_one(f, configuration) : 0;
    if ((dataSelection & 4) && configuration < 2) {
        qualify_one(f, configuration | 16);
        qualify_one(f, configuration | 48);
    }
    return clocks;
}

static void unpack_widths_offsets_and_complete_words(void) {
    const unsigned widths[] = {1, 2, 4, 8, 16, 32};
    const uint32_t offsets[] = {0, 3, 0x80000003, 0x7FFFFFFF};
    for (unsigned sourceWidth = 1; sourceWidth <= 8; sourceWidth *= 2) {
        for (unsigned d = 0; d < 6; d++) {
            if (widths[d] < sourceWidth) continue;
            for (unsigned bias = 0; bias < 4; bias++) {
                struct Fixture f = {0};
                f.call = 0x10;
                f.inputSize = f.consumed = 8;
                f.info[0] = 8;
                f.info[2] = (uint8_t)sourceWidth;
                f.info[3] = (uint8_t)widths[d];
                for (unsigned b = 0; b < 4; b++) f.info[4 + b] = (uint8_t)(offsets[bias] >> (b * 8));
                const uint8_t data[] = {0x81, 0xF1, 0xFF, 0, 1, 0x7F, 0x80, 0x55};
                memcpy(f.input, data, sizeof(data));
                uint32_t packed = 0;
                unsigned bits = 0;
                for (unsigned i = 0; i < 8; i++) {
                    for (unsigned shift = 0; shift < 8; shift += sourceWidth) {
                        uint32_t value = (data[i] >> shift) & ((1u << sourceWidth) - 1);
                        if (value || offsets[bias] >> 31) value += offsets[bias] & 0x7FFFFFFF;
                        packed |= value << bits;
                        bits += widths[d];
                        if (bits == 32) {
                            for (unsigned lane = 0; lane < 4; lane++) f.output[f.outputSize++] = (uint8_t)(packed >> (lane * 8));
                            packed = bits = 0;
                        }
                    }
                }
                for (unsigned configuration = 0; configuration < 12; configuration++) qualify(&f, configuration);
            }
        }
    }
}

static void lz_literals_overlap_and_displacements(void) {
    const unsigned distances[] = {1, 2, 3, 4, 7, 258, 4096};
    for (unsigned halfword = 0; halfword < 2; halfword++) {
        for (unsigned d = halfword; d < 7; d++) {
            for (unsigned odd = 0; odd < 2; odd++) {
                const unsigned prefix = distances[d] + odd, count = 18;
                struct Fixture f = {0};
                header(&f, 0x10, prefix + count + (prefix & 1));
                f.call = 0x11 + halfword;
                unsigned flag = 0, tokens = 8;
                for (unsigned token = 0; token < prefix + 1 + (prefix & 1); token++) {
                    if (tokens == 8) {flag = f.inputSize++; tokens = 0;}
                    if (token == prefix) {
                        f.input[flag] |= (uint8_t)(0x80 >> tokens);
                        f.input[f.inputSize++] = (uint8_t)(0xF0 | ((distances[d] - 1) >> 8));
                        f.input[f.inputSize++] = (uint8_t)(distances[d] - 1);
                        for (unsigned i = 0; i < count; i++) {
                            const uint8_t value = f.output[f.outputSize - distances[d]];
                            f.output[f.outputSize++] = value;
                        }
                    } else {
                        const uint8_t value = (uint8_t)(token * 77 + 0x81);
                        f.input[f.inputSize++] = value;
                        f.output[f.outputSize++] = value;
                    }
                    tokens++;
                }
                f.consumed = f.inputSize;
                for (unsigned configuration = 0; configuration < 12; configuration++) {
                    if (!halfword && configuration >= 8) continue;
                    qualify(&f, configuration);
                }
            }
        }
    }
}

static void huffman_leaf_widths_deep_tree_and_stream_words(void) {
    for (unsigned nibble = 0; nibble < 2; nibble++) {
        for (unsigned deep = 0; deep < 2; deep++) {
            struct Fixture f = {0};
            header(&f, nibble ? 0x24 : 0x28, 64);
            f.call = 0x13;
            f.input[4] = deep ? 3 : 1;
            f.input[5] = deep ? 0x80 : 0xC0;
            f.input[6] = nibble ? 1 : 0x81;
            f.input[7] = deep ? 0xC0 : (nibble ? 15 : 0xF1);
            if (deep) {
                f.input[8] = nibble ? 3 : 0x33;
                f.input[9] = nibble ? 15 : 0xF1;
            }
            const unsigned stream = deep ? 12 : 8;
            unsigned bits = 0;
            for (unsigned unit = 0; unit < (nibble ? 128u : 64u); unit++) {
                const unsigned symbol = deep ? unit % 3 : unit & 1;
                const unsigned code = deep && symbol ? (symbol == 1 ? 2 : 3) : symbol;
                const unsigned length = deep && symbol ? 2 : 1;
                for (unsigned j = length; j > 0; j--) {
                    const unsigned position = stream + (bits / 32) * 4 + (31 - bits % 32) / 8;
                    f.input[position] |= (uint8_t)(((code >> (j - 1)) & 1) << ((31 - bits % 32) % 8));
                    bits++;
                }
                const uint8_t value = symbol == 0 ? f.input[6] : deep && symbol == 1 ? f.input[8] : (nibble ? 15 : 0xF1);
                if (nibble) f.output[unit / 2] |= (uint8_t)(value << ((unit & 1) * 4));
                else f.output[unit] = value;
            }
            f.outputSize = 64;
            f.consumed = f.inputSize = stream + ((bits + 31) / 32) * 4;
            f.r3 = word(f.output + 60);
            for (unsigned configuration = 0; configuration < 12; configuration++) qualify(&f, configuration);
        }
    }
}

static void run_length_literal_and_repeated_groups(void) {
    for (unsigned halfword = 0; halfword < 2; halfword++) {
        for (unsigned length = 1; length <= 128; length += 127) {
            struct Fixture f = {0};
            header(&f, 0x30, length * 2 + 130);
            f.call = 0x14 + halfword;
            for (unsigned block = 0; block < 3; block++) {
                const unsigned size = block == 1 ? 130 : length;
                f.input[f.inputSize++] = block == 1 ? 0xFF : (uint8_t)(size - 1);
                if (block == 1) f.input[f.inputSize++] = 0xF1;
                for (unsigned i = 0; i < size; i++) {
                    const uint8_t value = block == 1 ? 0xF1 : (uint8_t)(i * 109 + block * 17);
                    if (block != 1) f.input[f.inputSize++] = value;
                    f.output[f.outputSize++] = value;
                }
            }
            f.r3 = 0x170;
            f.consumed = f.inputSize;
            for (unsigned configuration = 0; configuration < 12; configuration++) {
                if (!halfword && configuration >= 8) continue;
                qualify(&f, configuration);
            }
        }
    }
}

static void differential_wrapping_and_output_widths(void) {
    const unsigned lengths[] = {2, 8, 32, 128};
    for (unsigned swi = 0x16; swi <= 0x18; swi++) {
        for (unsigned n = 0; n < 4; n++) {
            const unsigned count = lengths[n];
            struct Fixture f = {0};
            header(&f, swi == 0x18 ? 0x82 : 0x81, count);
            f.call = swi;
            uint32_t sum = 0;
            const unsigned width = swi == 0x18 ? 2 : 1;
            for (unsigned i = 0; i < count; i += width) {
                const unsigned delta = width == 2 ? (i * 31123 + 0xF181) & 0xFFFF : (i * 109 + 0x81) & 0xFF;
                sum += delta;
                for (unsigned b = 0; b < width; b++) {
                    f.input[f.inputSize++] = (uint8_t)(delta >> (b * 8));
                    f.output[f.outputSize++] = (uint8_t)(sum >> (b * 8));
                }
                f.r3 = width == 2 ? delta : 0x170;
            }
            if (swi == 0x18 && count == 2) f.r3 = 0xBA4;
            f.consumed = f.inputSize;
            for (unsigned configuration = 0; configuration < 12; configuration++) {
                if (swi == 0x16 && configuration >= 8) continue;
                qualify(&f, configuration);
            }
        }
    }
}

static void zero_sizes_partial_words_and_uncommitted_bytes(void) {
    for (unsigned swi = 0x10; swi <= 0x18; swi++) {
        struct Fixture f = {0};
        header(&f, swi == 0x13 ? 0x28 : 0x10, 0);
        f.call = swi;
        f.consumed = 4;
        f.r3 = swi == 0x18 ? 0xBA4 : swi >= 0x14 ? 0x170 : swi == 0x11 || swi == 0x10 ? 0x11220003 : 0;
        if (swi == 0x10) {
            f.info[2] = f.info[3] = 8;
            f.inputSize = f.consumed = 0;
        } else if (swi == 0x13) {
            f.input[4] = 1;
            f.input[5] = 0xC0;
            f.input[6] = 0x81;
            f.input[7] = 0xF1;
            f.inputSize = f.consumed = 8;
        }
        for (unsigned configuration = 0; configuration < 8; configuration++) qualify(&f, configuration);
    }
    struct Fixture partial = {0};
    partial.call = 0x10;
    partial.inputSize = partial.consumed = 1;
    partial.input[0] = 0x81;
    partial.info[0] = 1;
    partial.info[2] = partial.info[3] = 8;
    partial.r3 = 8;
    for (unsigned configuration = 0; configuration < 12; configuration++) qualify(&partial, configuration);
    /* Byte counts are accepted; a pending high-level byte is not a bus write. */
    for (unsigned swi = 0x12; swi <= 0x17; swi++) {
        if (swi != 0x12 && swi != 0x15 && swi != 0x17) continue;
        struct Fixture f = {0};
        header(&f, swi == 0x12 ? 0x10 : swi == 0x15 ? 0x30 : 0x81, 5);
        f.call = swi;
        if (swi != 0x17) f.input[f.inputSize++] = swi == 0x12 ? 0 : 4;
        unsigned sum = 0;
        for (unsigned i = 0; i < 5; i++) {
            const uint8_t value = (uint8_t)(0x81 + i * 109);
            f.input[f.inputSize++] = value;
            sum += value;
            if (i < 4) f.output[f.outputSize++] = swi == 0x17 ? (uint8_t)sum : value;
            if (i == 4 && swi == 0x12) f.r3 = value;
        }
        if (swi != 0x12) f.r3 = 0x170;
        f.consumed = f.inputSize;
        for (unsigned configuration = 0; configuration < 12; configuration++) qualify(&f, configuration);
    }
}

static struct Result reject_source(unsigned swi, uint32_t source, bool thumb, const char* bios) {
    struct CupidGBA* gba = machine(bios);
    const uint8_t info[] = {8, 0, 8, 8, 0, 0, 0, 0};
    memcpy(gba->memory.ewram + 0x2F00, info, sizeof(info));
    memset(gba->memory.ewram + 0x3000, 0xA5, 16);
    gba->cpu.gprs[0] = (int32_t)source;
    gba->cpu.gprs[1] = GBA_BASE_EWRAM + 0x3000;
    gba->cpu.gprs[2] = GBA_BASE_EWRAM + 0x2F00;
    struct Result result = {0};
    result.clocks = invoke(gba, swi, thumb);
    CHECK((uint32_t)gba->cpu.gprs[0] == source + (swi == 0x10 || swi == 0x13 ? 0 : 4));
    CHECK(gba->cpu.gprs[1] == GBA_BASE_EWRAM + 0x3000);
    CHECK(gba->cpu.gprs[2] == GBA_BASE_EWRAM + 0x2F00);
    for (unsigned i = 0; i < 16; i++) CHECK(gba->memory.ewram[0x3000 + i] == 0xA5);
    const uint32_t r3 = swi == 0x18 ? 0xBA4 : swi >= 0x14 ? 0x170 : swi == 0x12 ? 0 : 0x11220003;
    CHECK((uint32_t)gba->cpu.gprs[3] == r3);
    memcpy(result.registers, gba->cpu.gprs, sizeof(result.registers));
    destroy(gba);
    return result;
}

static void protected_sources_leave_destinations_untouched(void) {
    for (unsigned swi = 0x10; swi <= 0x18; swi++) {
        for (unsigned configuration = 0; configuration < 4; configuration++) {
            const uint32_t source = configuration & 2 ? 0x01000000 : 0;
            const struct Result builtIn = reject_source(swi, source, configuration & 1, NULL);
            if (externalBios) {
                const struct Result external = reject_source(swi, source, configuration & 1, externalBios);
                CHECK(builtIn.clocks == external.clocks);
                CHECK(!memcmp(builtIn.registers, external.registers, sizeof(builtIn.registers)));
            }
            observations++;
        }
    }
}

static void fixed_clock_observations_for_small_valid_streams(void) {
    const uint8_t bytes[] = {0x81, 0xF1, 0xFF, 0, 1, 0x7F, 0x80, 0x55};
    const unsigned clocks[] = {445, 258, 296, 521, 218, 278, 206, 260, 146};
    for (unsigned swi = 0x10; swi <= 0x18; swi++) {
        struct Fixture f = {0};
        header(&f, swi == 0x13 ? 0x28 : swi >= 0x16 ? (swi == 0x18 ? 0x82 : 0x81) : swi >= 0x14 ? 0x30 : 0x10, 8);
        f.call = swi;
        if (swi == 0x10) {
            memcpy(f.input, bytes, sizeof(bytes));
            f.inputSize = 8;
            f.info[0] = f.info[2] = f.info[3] = 8;
        } else if (swi == 0x13) {
            const uint8_t tree[] = {1, 0xC0, 0x81, 0xF1, 0, 0, 0, 0xAA};
            memcpy(f.input + 4, tree, sizeof(tree));
            f.inputSize = 12;
            for (unsigned i = 0; i < 8; i++) f.output[i] = i & 1 ? 0x81 : 0xF1;
            f.r3 = 0x81F181F1;
        } else {
            if (swi < 0x16) f.input[f.inputSize++] = swi >= 0x14 ? 7 : 0;
            memcpy(f.input + f.inputSize, bytes, sizeof(bytes));
            f.inputSize += sizeof(bytes);
        }
        if (swi >= 0x16) {
            unsigned sum = 0, width = swi == 0x18 ? 2 : 1;
            for (unsigned i = 0; i < 8; i += width) {
                sum += bytes[i] | (width == 2 ? (unsigned)bytes[i + 1] << 8 : 0);
                for (unsigned b = 0; b < width; b++) f.output[i + b] = (uint8_t)(sum >> (b * 8));
            }
            f.r3 = swi == 0x18 ? 0x5580 : 0x170;
        } else if (swi != 0x13) {
            memcpy(f.output, bytes, sizeof(bytes));
            f.r3 = swi >= 0x14 ? 0x170 : swi == 0x11 ? 0x11220003 : 0;
        }
        f.outputSize = 8;
        f.consumed = f.inputSize;
        const uint64_t elapsed = qualify(&f, 0);
        if (dataSelection & 1) CHECK(elapsed == clocks[swi - 0x10]);
    }
}

struct BiasTrace {
    struct CupidGBA* gba;
    struct CupidTimingEvent sample, dma;
    uint64_t times[514];
    uint16_t values[514];
    unsigned count;
};

static void observe_bias(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct BiasTrace* trace = context;
    const uint16_t value = cupid_io_read(trace->gba, REG_SOUNDBIAS);
    if (!trace->count || trace->values[trace->count - 1] != value) {
        CHECK(trace->count < 514);
        trace->times[trace->count] = cupid_timing_global_time(timing);
        trace->values[trace->count++] = value;
    }
    cupid_timing_schedule(timing, &trace->sample, 1);
}

static void start_dma(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing;
    (void)late;
    struct CupidGBA* gba = context;
    cupid_io_write(gba, REG_DMA0CNT_HI, 0xC400);
}

static uint64_t bias_execute(unsigned level, unsigned fields, unsigned direction, bool thumb,
                             bool interrupts, const char* bios, struct BiasTrace* trace) {
    struct CupidGBA* gba = machine(bios);
    gba->cpu.gprs[0] = direction ? 0x1234 : 0;
    gba->cpu.gprs[1] = 0x76543210;
    cupid_io_write(gba, REG_SOUNDBIAS, (uint16_t)(fields | level));
    cupid_io_write(gba, REG_TM0CNT_L, interrupts ? 0xF000 : 0);
    cupid_io_write(gba, REG_TM0CNT_H, interrupts ? 0xC0 : 0x80);
    cupid_timing_tick(&gba->timing, 2);
    memset(trace, 0, sizeof(*trace));
    trace->gba = gba;
    trace->sample.context = trace;
    trace->sample.callback = observe_bias;
    trace->sample.priority = 0x60;
    cupid_timing_schedule(&gba->timing, &trace->sample, 1);
    if (interrupts) {
        const uint32_t handler[] = {
            0xE3A00301, 0xE2800C02, 0xE1D010B2, 0xE1C010B2,
            0xE59F200C, 0xE5923000, 0xE2833001, 0xE5823000,
            0xE12FFF1E, GBA_BASE_EWRAM + 0x2E00
        };
        memcpy(gba->memory.iwram + 0x7000, handler, sizeof(handler));
        const uint32_t entry = GBA_BASE_IWRAM + 0x7000;
        memcpy(gba->memory.iwram + 0x7FFC, &entry, sizeof(entry));
        for (unsigned i = 0; i < 64; i++) gba->memory.ewram[0x2D00 + i] = (uint8_t)(i * 77 + 0x81);
        memset(gba->memory.ewram + 0x2D7C, 0xA5, 72);
        cupid_io_write(gba, REG_DMA0SAD_LO, 0x2D00);
        cupid_io_write(gba, REG_DMA0SAD_HI, 0x0200);
        cupid_io_write(gba, REG_DMA0DAD_LO, 0x2D80);
        cupid_io_write(gba, REG_DMA0DAD_HI, 0x0200);
        cupid_io_write(gba, REG_DMA0CNT_LO, 16);
        trace->dma.context = gba;
        trace->dma.callback = start_dma;
        trace->dma.priority = 0x20;
        cupid_timing_schedule(&gba->timing, &trace->dma, 100);
        cupid_io_write(gba, REG_IE, IRQ_TIMER0 | IRQ_DMA0);
        cupid_io_write(gba, REG_IME, 1);
    }
    const uint64_t clocks = invoke(gba, 0x19, thumb);
    const unsigned target = direction ? (level < 512 ? 512 : level) : 0;
    const unsigned steps = (direction ? target - level : level) / 2;
    CHECK(cupid_io_read(gba, REG_SOUNDBIAS) == (fields | target));
    CHECK(gba->cpu.gprs[0] == (direction ? 0x1234 : 0));
    CHECK(gba->cpu.gprs[1] == (int32_t)target);
    CHECK((uint32_t)gba->cpu.gprs[2] == 0x11220002);
    CHECK((uint32_t)gba->cpu.gprs[3] == GBA_BASE_IO + REG_SOUNDBIAS);
    CHECK(trace->count == steps + 1);
    for (unsigned i = 0; i < trace->count; i++) {
        CHECK(trace->values[i] == (fields | (direction ? level + 2 * i : level - 2 * i)));
        if (i) CHECK(trace->times[i] > trace->times[i - 1]);
    }
    if (!interrupts) {
        CHECK(cupid_io_read(gba, REG_TM0CNT_L) == (uint16_t)clocks);
        if (!direction && !thumb) CHECK(clocks == steps * 61 + 73);
    } else {
        CHECK(clocks > steps * 61 + 73);
        CHECK(!memcmp(gba->memory.ewram + 0x2D00, gba->memory.ewram + 0x2D80, 64));
        for (unsigned i = 0; i < 4; i++) CHECK(gba->memory.ewram[0x2D7C + i] == 0xA5 && gba->memory.ewram[0x2DC0 + i] == 0xA5);
        CHECK(word(gba->memory.ewram + 0x2E00) == clocks / 4096 + 1);
        CHECK(gba->irqf == 0);
    }
    destroy(gba);
    trace->gba = NULL;
    return clocks;
}

static void bias_ramps_resolution_fields_and_peripheral_events(void) {
    const unsigned levels[] = {0, 2, 510, 512, 514, 1022};
    for (unsigned l = 0; l < 6; l++) {
        for (unsigned configuration = 0; configuration < 16; configuration++) {
            const unsigned fields = (configuration >> 2) << 14;
            const bool thumb = configuration & 1;
            const unsigned direction = (configuration >> 1) & 1;
            struct BiasTrace builtIn, external;
            const uint64_t clocks = bias_execute(levels[l], fields, direction, thumb, false, NULL, &builtIn);
            if (externalBios) {
                CHECK(clocks == bias_execute(levels[l], fields, direction, thumb, false, externalBios, &external));
                CHECK(builtIn.count == external.count);
                CHECK(!memcmp(builtIn.values, external.values, builtIn.count * sizeof(builtIn.values[0])));
                CHECK(!memcmp(builtIn.times, external.times, builtIn.count * sizeof(builtIn.times[0])));
            }
            observations++;
        }
    }
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct BiasTrace builtIn, external;
        const uint64_t clocks = bias_execute(512, 0xC000, 0, thumb, true, NULL, &builtIn);
        if (externalBios) {
            CHECK(clocks == bias_execute(512, 0xC000, 0, thumb, true, externalBios, &external));
            CHECK(builtIn.count == external.count);
            CHECK(!memcmp(builtIn.times, external.times, builtIn.count * sizeof(builtIn.times[0])));
        }
        observations++;
    }
}

int main(int argc, char** argv) {
    bool bias = false;
    for (int argument = 1; argument < argc; argument++) {
        if (!strcmp(argv[argument], "--bias")) bias = true;
        else if (!strcmp(argv[argument], "--ewram")) dataSelection = 1;
        else if (!strcmp(argv[argument], "--iwram")) dataSelection = 2;
        else if (!strcmp(argv[argument], "--ram")) dataSelection = 3;
        else if (!strcmp(argv[argument], "--cartridge")) dataSelection = 4;
        else if (!strcmp(argv[argument], "--external-bios") && argument + 1 < argc) externalBios = argv[++argument];
        else return 2;
    }
    if (bias) {
        RUN(bias_ramps_resolution_fields_and_peripheral_events);
        CHECK(observations == 98);
        printf("PASS %u bias observations\n", observations);
        return 0;
    }
    RUN(unpack_widths_offsets_and_complete_words);
    RUN(lz_literals_overlap_and_displacements);
    RUN(huffman_leaf_widths_deep_tree_and_stream_words);
    RUN(run_length_literal_and_repeated_groups);
    RUN(differential_wrapping_and_output_widths);
    RUN(zero_sizes_partial_words_and_uncommitted_bytes);
    if (dataSelection & 1) {
        RUN(protected_sources_leave_destinations_untouched);
    }
    if (dataSelection & 5) RUN(fixed_clock_observations_for_small_valid_streams);
    CHECK(observations == ((dataSelection & 1 ? 773u : 0) +
                          (dataSelection & 2 ? 728u : 0) +
                          (dataSelection & 4 ? 542u : 0)));
    printf("PASS %u utility observations\n", observations);
    return 0;
}
