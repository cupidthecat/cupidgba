/**
 * @file      test_multiply.c
 * @author    cupidthecat
 * @brief     Multiply result, carry, condition, alias and internal-cycle regressions.
 * @date      2026-11-16
 *
 * @copyright Copyright (c) 2026 cupidthecat
 *
 * This file is part of CupidGBA.
 *
 * CupidGBA is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CupidGBA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CupidGBA.  If not, see <https://gnu.org>.
 */
#include "../test.h"
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"

enum MultiplyKind { MUL, MLA, UMULL, UMLAL, SMULL, SMLAL };

struct Vector {
    uint32_t a, b, low, high;
    unsigned carry; /* MUL, MLA, UMULL, UMLAL, SMULL, SMLAL, least significant first. */
};

struct Registers {
    unsigned rm, rs, low, high, accumulator;
};
static const struct Registers ordinary = {0, 1, 2, 3, 4};
static const uint32_t opcodes[] = {0xE0000090, 0xE0200090, 0xE0800090,
                                   0xE0A00090, 0xE0C00090, 0xE0E00090};
static const uint64_t epochs[] = {0, INT32_MAX - UINT64_C(4), UINT32_MAX - UINT64_C(4),
                                  UINT64_C(0x300000000)};
/* Fixed carry vectors checked independently of the implementation. They cover
 * each termination length, Booth signs, accumulator carry and signed extension.
 * These supplement the hardware observations below; they are model comparisons. */
static const struct Vector vectors[] = {
    {0x6FBAD8A7u, 0x00000000u, 0xA16A7CF0u, 0x30757ACAu, 0x00},
    {0x7AA2DEDCu, 0x00000001u, 0x0E508260u, 0xDD8D20AEu, 0x00},
    {0x4A835022u, 0x00000002u, 0x30670C41u, 0xEC07D276u, 0x00},
    {0xDF6A3312u, 0x00000003u, 0xB80FCD56u, 0x75009D04u, 0x00},
    {0xB2A5E4D4u, 0x0000007Fu, 0xC9D31AABu, 0xFA43D2E8u, 0x15},
    {0x83CCCE07u, 0x00000080u, 0x5B884321u, 0x2E651037u, 0x2A},
    {0x001576C6u, 0x000000FFu, 0x7771D5A0u, 0xF57B9B02u, 0x00},
    {0x456899EFu, 0x00000100u, 0x9CFF0A65u, 0xCBC1033Cu, 0x2A},
    {0x9F782B0Fu, 0x00007FFFu, 0xD9294663u, 0xCD4E8CB1u, 0x00},
    {0x87CCF97Du, 0x00008000u, 0x0489F48Cu, 0x79155980u, 0x00},
    {0x968322D2u, 0x0000FFFFu, 0xA9FA6C7Eu, 0x6ACE16C4u, 0x00},
    {0xAACE9B2Fu, 0x00010000u, 0x4CC3B8BAu, 0x41484F50u, 0x00},
    {0x5CEF0B21u, 0x007FFFFFu, 0x0CE5C964u, 0x0CEB6380u, 0x00},
    {0x73F12A6Du, 0x00800000u, 0x81288753u, 0xA99759F2u, 0x00},
    {0x178E2C66u, 0x00FFFFFFu, 0x92522987u, 0x3B36E4F6u, 0x3F},
    {0x12A2B362u, 0x01000000u, 0xDD14F1A5u, 0x272E7695u, 0x00},
    {0xD668EF0Bu, 0x7FFFFFFFu, 0xBAB4B32Fu, 0xB6F9EDA7u, 0x14},
    {0xE2E44821u, 0x80000000u, 0x17EBE7F1u, 0x09ABBD1Bu, 0x2F},
    {0xBF14A1FFu, 0x80000001u, 0x4E41D92Au, 0xD9F0D098u, 0x0F},
    {0xBF959C49u, 0xBFFFFFFFu, 0x8F8BF2A7u, 0xCA322348u, 0x2F},
    {0x4537E8C5u, 0xC0000000u, 0xBDCD8E92u, 0x8FE6DB3Du, 0x0C},
    {0xC4AB7EDDu, 0xFFFFFF00u, 0xC561E7C5u, 0xCABE5A29u, 0x2E},
    {0x3E942054u, 0xFFFF0000u, 0xE9415E7Bu, 0x9345B8BCu, 0x2A},
    {0xAE177835u, 0xFF000000u, 0x632EEF1Du, 0x676F1F1Bu, 0x22},
    {0x150B96FDu, 0xFFFFFFFFu, 0x9D54A7F7u, 0x3CE22762u, 0x22},
    {0x79C537C5u, 0xFFFFFFFEu, 0x3882F33Bu, 0xBA51A969u, 0x04},
    {0x60E51437u, 0x12345678u, 0x8E1135C6u, 0xBD9D93D2u, 0x00},
    {0xF31D5001u, 0xFEDCBA98u, 0x7AB6CD6Fu, 0xC074E0A2u, 0x04},
    {0xD0715CD2u, 0x0000007Fu, 0xD0715CD2u, 0x0000007Fu, 0x00},
    {0x0E2EF5C3u, 0x00000080u, 0x0E2EF5C3u, 0x00000080u, 0x00},
    {0x3E163675u, 0x00FFFFFFu, 0x3E163675u, 0x00FFFFFFu, 0x3F},
    {0xAC27D447u, 0xFFFFFFFFu, 0xAC27D447u, 0xFFFFFFFFu, 0x00},

};

static struct CupidGBA *machine(uint64_t epoch) {
    struct CupidGBA *gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = epoch;
    gba->timing.masterCycles = (uint32_t)epoch;
    gba->audio.lastSample = gba->audio.sampleOrigin = epoch;
    gba->audio.nextSample = epoch + 512;
    cupid_arm_set_priv_mode(&gba->cpu, MODE_SYS);
    gba->cpu.cpsr.packed = MODE_SYS | 0xC0;
    gba->cpu.irqDisableLatch = true;
    return gba;
}

static void destroy(struct CupidGBA *gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void program(struct CupidGBA *gba, const void *code, size_t size, bool thumb) {
    memcpy(gba->memory.iwram + 0x1000, code, size);
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x1000;
    if (thumb)
        CupidThumbWritePC(&gba->cpu);
    else
        CupidARMWritePC(&gba->cpu);
}

static void flags(struct CupidGBA *gba, unsigned value, unsigned shifter) {
    gba->cpu.cpsr.packed = (gba->cpu.cpsr.packed & 0x0FFFFFFF) | value << 28;
    gba->cpu.shifterCarryOut = (int32_t)shifter;
}

static bool condition_passes(unsigned condition, unsigned value) {
    bool n = value & 8, z = value & 4, c = value & 2, v = value & 1;
    const bool result[] = {z,    !z,      c,       !c,     n,      !n,           v,
                           !v,   c && !z, !c || z, n == v, n != v, !z && n == v, z || n != v,
                           true, false};
    return result[condition];
}

static unsigned internal_cycles(enum MultiplyKind kind, uint32_t multiplier) {
    bool sign = kind != UMULL && kind != UMLAL;
    unsigned count = 4;
    if (multiplier < 0x100 || (sign && multiplier >= 0xFFFFFF00))
        count = 1;
    else if (multiplier < 0x10000 || (sign && multiplier >= 0xFFFF0000))
        count = 2;
    else if (multiplier < 0x1000000 || (sign && multiplier >= 0xFF000000))
        count = 3;
    return count + (kind >= UMULL) + (kind & 1);
}

static uint64_t product(enum MultiplyKind kind, const struct Vector *vector) {
    uint64_t value = kind >= SMULL
                         ? (uint64_t)((int64_t)(int32_t)vector->a * (int64_t)(int32_t)vector->b)
                         : (uint64_t)vector->a * vector->b;
    if (kind & 1)
        value += kind == MLA ? vector->low : ((uint64_t)vector->high << 32) | vector->low;
    return kind < UMULL ? (uint32_t)value : value;
}

static uint32_t prepare(struct CupidGBA *gba, enum MultiplyKind kind, const struct Vector *vector,
                        struct Registers regs, bool setFlags, unsigned incoming, unsigned shifter) {
    for (unsigned r = 0; r < ARM_PC; ++r)
        gba->cpu.gprs[r] = (int32_t)(0x86750000u + r);
    gba->cpu.gprs[regs.low] = (int32_t)vector->low;
    gba->cpu.gprs[regs.high] = (int32_t)vector->high;
    if (kind == MLA)
        gba->cpu.gprs[regs.accumulator] = (int32_t)vector->low;
    gba->cpu.gprs[regs.rm] = (int32_t)vector->a;
    gba->cpu.gprs[regs.rs] = (int32_t)vector->b;
    if (kind & 1) {
        CHECK((uint32_t)gba->cpu.gprs[kind == MLA ? regs.accumulator : regs.low] == vector->low);
        if (kind != MLA)
            CHECK((uint32_t)gba->cpu.gprs[regs.high] == vector->high);
    }
    flags(gba, incoming, shifter);
    return opcodes[kind] | (uint32_t)setFlags << 20 | regs.rs << 8 | regs.rm |
           (kind < UMULL ? regs.low << 16 : regs.high << 16 | regs.low << 12) |
           (kind == MLA ? regs.accumulator << 12 : 0);
}

static void check_result(struct CupidGBA *gba, enum MultiplyKind kind, const struct Vector *vector,
                         struct Registers regs, const int32_t *before, unsigned incoming,
                         bool setFlags, bool checkCarry, bool executed) {
    uint64_t value = product(kind, vector);
    for (unsigned r = 0; r < ARM_PC; ++r) {
        uint32_t expected = (uint32_t)before[r];
        if (executed && r == regs.low)
            expected = (uint32_t)value;
        if (executed && kind >= UMULL && r == regs.high)
            expected = (uint32_t)(value >> 32);
        CHECK((uint32_t)gba->cpu.gprs[r] == expected);
    }
    unsigned expected = incoming;
    if (executed && setFlags)
        expected = (incoming & 1) | ((vector->carry >> kind & 1) << 1) | (!value ? 4 : 0) |
                   ((value >> (kind < UMULL ? 31 : 63)) ? 8 : 0);
    unsigned mask = checkCarry || !setFlags || !executed ? 15 : 13;
    unsigned actual = (uint32_t)gba->cpu.cpsr.packed >> 28;
    if ((actual & mask) != (expected & mask))
        fprintf(stderr,
                "kind=%u a=%08" PRIx32 " b=%08" PRIx32 " accumulator=%08" PRIx32 "%08" PRIx32
                " incoming=%x NZCV=%x expected=%x\n",
                kind, vector->a, vector->b, vector->high, vector->low, incoming, actual, expected);
    CHECK((actual & mask) == (expected & mask));
}

static void arm_case(struct CupidGBA *gba, enum MultiplyKind kind, const struct Vector *vector,
                     struct Registers regs, bool setFlags, unsigned incoming, unsigned shifter,
                     unsigned condition) {
    uint32_t opcode = prepare(gba, kind, vector, regs, setFlags, incoming, shifter);
    opcode = (opcode & 0x0FFFFFFF) | condition << 28;
    const uint32_t code[] = {opcode, 0xE1A00000, 0xE1A00000, 0xEAFFFFFE};
    program(gba, code, sizeof(code), false);
    int32_t before[ARM_PC];
    memcpy(before, gba->cpu.gprs, sizeof(before));
    uint64_t clock = cupid_timing_global_time(&gba->timing);
    cupid_gba_step(gba);
    bool executed = condition_passes(condition, incoming);
    check_result(gba, kind, vector, regs, before, incoming, setFlags, true, executed);
    CHECK(cupid_timing_global_time(&gba->timing) - clock ==
          1 + (executed ? internal_cycles(kind, vector->b) : 0));
}

/* The 36 operand pairs and both long-multiply signedness modes are independent,
 * pinned hardware observations. Rows select Rs, columns select Rm. Carry is
 * recorded separately from the ordinary 64-bit arithmetic and N/Z calculation. */
static void hardware(void) {
    const uint32_t values[] = {0, 1, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x80000001};
    const unsigned signedCarry[] = {0, 0, 0, 0x34, 0x0B, 0x0B};
    const unsigned unsignedCarry[] = {0, 0, 0x0C, 0x34, 0x34, 0x34};
    struct CupidGBA *gba = machine(0);
    unsigned observations = 0;
    for (unsigned b = 0; b < 6; ++b)
        for (unsigned a = 0; a < 6; ++a)
            for (unsigned sign = 0; sign < 2; ++sign) {
                enum MultiplyKind kind = sign ? SMULL : UMULL;
                unsigned carry = ((sign ? signedCarry[b] : unsignedCarry[b]) >> a) & 1;
                const struct Vector vector = {values[a], values[b], 0, 0, carry << kind};
                for (unsigned incoming = 0; incoming < 16; ++incoming)
                    for (unsigned shifter = 0; shifter < 2; ++shifter)
                        arm_case(gba, kind, &vector, ordinary, true, incoming, shifter, 14);
                ++observations;
            }
    printf("%u hardware observations, each with 16 incoming NZCV values and two shifter carries\n",
           observations);
    destroy(gba);
}

static void arithmetic(void) {
    struct CupidGBA *gba = machine(0);
    for (unsigned i = 0; i < sizeof(vectors) / sizeof(*vectors); ++i)
        for (enum MultiplyKind kind = MUL; kind <= SMLAL; ++kind)
            for (unsigned incoming = 0; incoming < 16; ++incoming)
                arm_case(gba, kind, &vectors[i], ordinary, false, incoming, incoming & 1, 14);
    destroy(gba);
}

static void short_forms(void) {
    struct CupidGBA *gba = machine(0);
    for (unsigned i = 0; i < sizeof(vectors) / sizeof(*vectors); ++i) {
        for (enum MultiplyKind kind = MUL; kind <= SMLAL; ++kind)
            for (unsigned incoming = 0; incoming < 16; ++incoming)
                arm_case(gba, kind, &vectors[i], ordinary, true, incoming, (incoming >> 1) & 1, 14);
        for (unsigned incoming = 0; incoming < 16; ++incoming) {
            const uint16_t code[] = {0x4341, 0x46C0, 0x46C0, 0xE7FE}; /* MUL r1,r0 */
            gba->cpu.gprs[0] = (int32_t)vectors[i].a;
            gba->cpu.gprs[1] = (int32_t)vectors[i].b;
            flags(gba, incoming, !((incoming >> 1) & 1));
            program(gba, code, sizeof(code), true);
            uint64_t clock = cupid_timing_global_time(&gba->timing);
            cupid_gba_step(gba);
            uint32_t value = vectors[i].a * vectors[i].b;
            CHECK((uint32_t)gba->cpu.gprs[0] == vectors[i].a &&
                  (uint32_t)gba->cpu.gprs[1] == value);
            unsigned expected = (incoming & 1) | ((vectors[i].carry & 1) << 1) | (!value ? 4 : 0) |
                                ((value >> 31) ? 8 : 0);
            CHECK((uint32_t)gba->cpu.cpsr.packed >> 28 == expected);
            CHECK(cupid_timing_global_time(&gba->timing) - clock ==
                  1 + internal_cycles(MUL, vectors[i].b));
        }
    }
    destroy(gba);
}

static void aliases(void) {
    const struct Registers overlap[] = {{0, 1, 0, 3, 4}, {0, 1, 1, 3, 4}, {0, 1, 2, 0, 4},
                                        {0, 1, 2, 1, 4}, {0, 1, 1, 0, 4}, {0, 1, 4, 3, 4}};
    struct CupidGBA *gba = machine(0);
    for (unsigned i = 0; i < sizeof(vectors) / sizeof(*vectors); ++i)
        for (enum MultiplyKind kind = MUL; kind <= SMLAL; ++kind) {
            /* Long accumulation shares physical result and accumulator ports;
             * the last four vectors have matching source/accumulator values. */
            if ((kind == UMLAL || kind == SMLAL) && i < 28)
                continue;
            for (unsigned r = 0; r < sizeof(overlap) / sizeof(*overlap); ++r) {
                struct Registers regs = overlap[r];
                if ((kind == UMLAL || kind == SMLAL) && (r == 1 || r == 2 || r == 4))
                    continue;
                for (unsigned incoming = 0; incoming < 4; ++incoming)
                    arm_case(gba, kind, &vectors[i], regs, true, incoming, !((incoming >> 1) & 1),
                             14);
            }
        }
    destroy(gba);
}

static void conditions(void) {
    struct CupidGBA *gba = machine(0);
    for (unsigned condition = 0; condition < 16; ++condition)
        for (unsigned incoming = 0; incoming < 16; ++incoming)
            for (enum MultiplyKind kind = MUL; kind <= SMLAL; ++kind)
                for (unsigned setFlags = 0; setFlags < 2; ++setFlags)
                    arm_case(gba, kind, &vectors[17], ordinary, setFlags != 0, incoming,
                             !((incoming >> 1) & 1), condition);
    for (unsigned selected = 0; selected < 2; ++selected) {
        const struct Vector *vector = &vectors[selected ? 17 : 0];
        for (enum MultiplyKind kind = MUL; kind <= SMLAL; ++kind) {
            unsigned carry = vector->carry >> kind & 1;
            uint32_t opcode = prepare(gba, kind, vector, ordinary, true, carry ? 0 : 2, !carry);
            const uint32_t code[] = {opcode, 0x23A08031, 0x33A08072, 0xE1A00000, 0xEAFFFFFE};
            program(gba, code, sizeof(code), false);
            for (unsigned step = 0; step < 3; ++step)
                cupid_gba_step(gba);
            CHECK(gba->cpu.gprs[8] == (carry ? 0x31 : 0x72));
        }
        const uint16_t code[] = {0x4341, 0xD201, 0x2211, 0xE000, 0x2222, 0x46C0, 0xE7FE};
        gba->cpu.gprs[0] = (int32_t)vector->a;
        gba->cpu.gprs[1] = (int32_t)vector->b;
        flags(gba, selected ? 0 : 2, !selected);
        program(gba, code, sizeof(code), true);
        cupid_gba_step(gba);
        cupid_gba_step(gba);
        CHECK(cupid_arm_instruction_address(&gba->cpu) ==
              GBA_BASE_IWRAM + 0x1000 + (selected ? 8 : 4));
        cupid_gba_step(gba);
        CHECK(gba->cpu.gprs[2] == (selected ? 0x22 : 0x11));
    }
    /* Preserve the existing skipped behavior for PC destination/accumulator
     * encodings. These checks do not establish behavior for unpredictable
     * hardware encodings or introduce a multiply exception-return path. */
    for (enum MultiplyKind kind = MUL; kind <= SMLAL; ++kind)
        for (unsigned field = 0; field < (kind == MUL ? 1u : 2u); ++field) {
            uint32_t opcode = prepare(gba, kind, &vectors[17], ordinary, true, 11, 0);
            unsigned shift = field ? 12 : 16;
            opcode = (opcode & ~(15u << shift)) | 15u << shift;
            const uint32_t code[] = {opcode, 0xE1A00000, 0xE1A00000};
            program(gba, code, sizeof(code), false);
            int32_t before[ARM_PC];
            memcpy(before, gba->cpu.gprs, sizeof(before));
            union CupidPSR status = gba->cpu.cpsr;
            uint64_t clock = cupid_timing_global_time(&gba->timing);
            cupid_gba_step(gba);
            CHECK(!memcmp(before, gba->cpu.gprs, sizeof(before)));
            CHECK(gba->cpu.cpsr.packed == status.packed);
            CHECK(cupid_timing_global_time(&gba->timing) == clock + 1);
        }
    destroy(gba);
}

static void start_dma(struct CupidGBA *gba) {
    cupid_memory_store16(&gba->cpu, GBA_BASE_IWRAM + 0x2000, 0xB17D, NULL);
    cupid_dma_write_sad(gba, 0, GBA_BASE_IWRAM + 0x2000);
    cupid_dma_write_dad(gba, 0, GBA_BASE_IWRAM + 0x2100);
    cupid_dma_write_cnt_lo(gba, 0, 1);
    cupid_dma_write_cnt(gba, 0, 0xC000);
}

static void chronology(void) {
    const unsigned selections[] = {1, 7, 11, 15, 24};
    for (unsigned epoch = 0; epoch < sizeof(epochs) / sizeof(*epochs); ++epoch)
        for (unsigned selection = 0; selection < sizeof(selections) / sizeof(*selections);
             ++selection)
            for (enum MultiplyKind kind = MUL; kind <= SMLAL; ++kind)
                for (unsigned dma = 0; dma < 2; ++dma) {
                    const struct Vector *vector = &vectors[selections[selection]];
                    struct CupidGBA *pair[2] = {machine(epochs[epoch]), machine(epochs[epoch])};
                    for (unsigned setting = 0; setting < 2; ++setting) {
                        struct CupidGBA *gba = pair[setting];
                        uint32_t opcode = prepare(gba, kind, vector, ordinary, setting != 0, 3, 0);
                        const uint32_t code[] = {opcode, 0xE1A00000, 0xE1A00000, 0xE1A00000,
                                                 0xEAFFFFFE};
                        program(gba, code, sizeof(code), false);
                        int32_t before[ARM_PC];
                        memcpy(before, gba->cpu.gprs, sizeof(before));
                        if (dma)
                            start_dma(gba);
                        cupid_gba_step(gba);
                        CHECK(cupid_timing_global_time(&gba->timing) ==
                              epochs[epoch] + 1 + internal_cycles(kind, vector->b));
                        check_result(gba, kind, vector, ordinary, before, 3, setting != 0, false,
                                     true);
                    }
                    CHECK(pair[0]->memory.dmaRunning == pair[1]->memory.dmaRunning);
                    CHECK(pair[0]->irqf == pair[1]->irqf &&
                          pair[0]->irq.pendingIF == pair[1]->irq.pendingIF);
                    for (unsigned step = 0; step < 3; ++step) {
                        cupid_gba_step(pair[0]);
                        cupid_gba_step(pair[1]);
                        CHECK(cupid_timing_global_time(&pair[0]->timing) ==
                              cupid_timing_global_time(&pair[1]->timing));
                        CHECK(pair[0]->timing.masterCycles == pair[1]->timing.masterCycles);
                        CHECK(pair[0]->memory.dmaRunning == pair[1]->memory.dmaRunning);
                        CHECK(pair[0]->irqf == pair[1]->irqf &&
                              pair[0]->irq.pendingIF == pair[1]->irq.pendingIF);
                        CHECK(!memcmp(pair[0]->cpu.gprs, pair[1]->cpu.gprs,
                                      sizeof(pair[0]->cpu.gprs)));
                    }
                    if (dma) {
                        for (unsigned setting = 0; setting < 2; ++setting) {
                            CHECK(!pair[setting]->memory.dmaRunning);
                            CHECK(pair[setting]->irqf == IRQ_DMA0);
                            CHECK(cupid_memory_load16(&pair[setting]->cpu, GBA_BASE_IWRAM + 0x2100,
                                                      NULL) == 0xB17D);
                        }
                        CHECK(pair[0]->memory.dma[0].busyUntil == pair[1]->memory.dma[0].busyUntil);
                    }
                    destroy(pair[0]);
                    destroy(pair[1]);
                }
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "hardware"))
        RUN(hardware);
    else if (!strcmp(argv[1], "arithmetic"))
        RUN(arithmetic);
    else if (!strcmp(argv[1], "short"))
        RUN(short_forms);
    else if (!strcmp(argv[1], "aliases"))
        RUN(aliases);
    else if (!strcmp(argv[1], "conditions"))
        RUN(conditions);
    else if (!strcmp(argv[1], "chronology"))
        RUN(chronology);
    else
        CHECK(false);
    return 0;
}
