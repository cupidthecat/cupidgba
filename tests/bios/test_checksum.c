/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"
#include "caller.h"

static const char* externalBios;
static const uint32_t identity = 0xBAAE187F;
static const enum CupidPrivMode banks[] = {MODE_USR, MODE_FIQ, MODE_IRQ, MODE_SVC, MODE_ABT, MODE_UND, MODE_SYS};

static uint32_t word(const uint8_t* data) {
    uint32_t value;
    memcpy(&value, data, sizeof(value));
    return value;
}
static void put_word(uint8_t* data, uint32_t value) { memcpy(data, &value, sizeof(value)); }

static uint32_t stack(enum CupidPrivMode mode) {
    switch (mode) {
    case MODE_SVC: return GBA_SP_BASE_SUPERVISOR;
    case MODE_IRQ: return GBA_SP_BASE_IRQ;
    case MODE_FIQ: return GBA_BASE_IWRAM + 0x7C00;
    case MODE_ABT: return GBA_BASE_IWRAM + 0x7B80;
    case MODE_UND: return GBA_BASE_IWRAM + 0x7B00;
    default: return GBA_SP_BASE_SYSTEM;
    }
}

static struct CupidGBA* machine(uint64_t clock) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    if (externalBios) CHECK(cupid_load_bios(gba, externalBios));
    cupid_gba_reset(gba);
    cupid_test_bios_caller(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
    return gba;
}
static void destroy(struct CupidGBA* gba) { cupid_gba_destroy(gba); free(gba); }

static void prepare(struct CupidGBA* gba, bool thumb, enum CupidPrivMode mode, bool masked, uint32_t address) {
    const uint32_t arm[] = {0xEF0D0000, 0xE1A00000, 0xE1A00000, 0xEAFFFFFE};
    const uint16_t small[] = {0xDF0D, 0x46C0, 0x46C0, 0xE7FE};
    uint8_t* code = address == GBA_BASE_EWRAM ? gba->memory.ewram : gba->memory.iwram;
    if (address >= GBA_BASE_ROM0) {
        gba->memory.rom = calloc(1, 4096);
        CHECK(gba->memory.rom);
        gba->memory.romSize = 4096;
        gba->memory.romMask = 4095;
        code = gba->memory.rom;
    }
    memcpy(code, thumb ? (const void*)small : (const void*)arm, thumb ? sizeof(small) : sizeof(arm));
    cupid_arm_set_priv_mode(&gba->cpu, mode);
    for (unsigned r = 0; r < 15; r++) gba->cpu.gprs[r] = (int32_t)(0x11220000u + r);
    gba->cpu.gprs[ARM_SP] = (int32_t)stack(mode);
    gba->cpu.cpsr.packed = 0xA0000000u | mode | (masked ? 0xC0u : 0u);
    gba->cpu.spsr.packed = 0x6000003F;
    gba->cpu.irqDisableLatch = masked;
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = (int32_t)address;
    if (thumb) CupidThumbWritePC(&gba->cpu);
    else CupidARMWritePC(&gba->cpu);
    cupid_io_write(gba, REG_TM0CNT_L, 0);
    cupid_io_write(gba, REG_TM0CNT_H, 0x80);
    cupid_timing_tick(&gba->timing, 2);
}

static uint64_t call(struct CupidGBA* gba, bool thumb, enum CupidPrivMode mode, uint32_t address) {
    const uint64_t start = cupid_timing_global_time(&gba->timing);
    unsigned steps;
    for (steps = 0; steps < 40000; steps++) {
        cupid_gba_step(gba);
        if (gba->cpu.privilegeMode == mode && gba->cpu.executionMode == (thumb ? MODE_THUMB : MODE_ARM) &&
            (uint32_t)gba->cpu.gprs[ARM_PC] == address + (thumb ? 4u : 8u)) break;
    }
    CHECK(steps < 40000 && !gba->cpu.halted && !gba->stopped);
    return cupid_timing_global_time(&gba->timing) - start;
}

static void returned(struct CupidGBA* gba, uint32_t sum, bool thumb, enum CupidPrivMode mode, bool masked,
                     uint32_t address) {
    const uint32_t status = 0xA0000000u | mode | (thumb ? 0x20u : 0u) | (masked ? 0xC0u : 0u);
    CHECK(gba->cpu.privilegeMode == mode && (uint32_t)gba->cpu.cpsr.packed == status);
    for (unsigned r = 0; r < 15; r++) {
        uint32_t expected = 0x11220000u + r;
        if (r == 0) expected = sum;
        else if (r == 1) expected = 1;
        else if (r == 3) expected = GBA_SIZE_BIOS;
        else if (r == ARM_SP) expected = stack(mode);
        else if (r == ARM_LR && mode == MODE_SVC) expected = address + (thumb ? 2u : 4u);
        CHECK((uint32_t)gba->cpu.gprs[r] == expected);
    }
    if (mode == MODE_SVC) CHECK((uint32_t)gba->cpu.spsr.packed == status);
    else if (mode != MODE_SYS && mode != MODE_USR) CHECK((uint32_t)gba->cpu.spsr.packed == 0x6000003F);
    CHECK(gba->memory.biosPrefetch == 0xE3A02004);
}

static void caller_bank(unsigned bank) {
    CHECK(bank < sizeof(banks) / sizeof(banks[0]));
    const uint64_t clocks[] = {0, UINT64_C(0xFFFFFFF0), UINT64_C(0x1FFFFFFF0)};
    unsigned count = 0;
    for (unsigned thumb = 0; thumb < 2; thumb++) {
      for (unsigned masked = 0; masked < 2; masked++) {
        for (unsigned c = 0; c < 3; c++) {
            struct CupidGBA* gba = machine(clocks[c]);
            prepare(gba, thumb != 0, banks[bank], masked != 0, GBA_BASE_IWRAM);
            CHECK(call(gba, thumb != 0, banks[bank], GBA_BASE_IWRAM) == 41011);
            returned(gba, identity, thumb != 0, banks[bank], masked != 0, GBA_BASE_IWRAM);
            CHECK(cupid_timer_read_counter(gba, 0) == 41011);
            CHECK(cupid_timing_global_time(&gba->timing) == clocks[c] + 2 + 41011);
            destroy(gba);
            count++;
        }
      }
    }
    CHECK(count == 12);
}

static void caller_sources(unsigned thumb) {
    CHECK(thumb < 2);
    const uint32_t addresses[] = {GBA_BASE_EWRAM, GBA_BASE_IWRAM, GBA_BASE_ROM0, GBA_BASE_ROM1, GBA_BASE_ROM2};
    const unsigned elapsed[2][2][5] = {
        {{41028, 41011, 41032, 41042, 41062}, {41028, 41011, 41027, 41027, 41027}},
        {{41019, 41011, 41023, 41027, 41035}, {41019, 41011, 41021, 41021, 41021}}
    };
    const uint64_t clocks[] = {0, UINT64_C(0xFFFFFFF0), UINT64_C(0x1FFFFFFF0)};
    unsigned count = 0;
    for (unsigned fast = 0; fast < 2; fast++) {
      for (unsigned region = 0; region < 5; region++) {
        for (unsigned c = 0; c < 3; c++) {
            struct CupidGBA* gba = machine(clocks[c]);
            cupid_memory_update_waitstates(gba, fast ? 0x0490 : 0);
            prepare(gba, thumb != 0, MODE_SYS, false, addresses[region]);
            CHECK(call(gba, thumb != 0, MODE_SYS, addresses[region]) == elapsed[thumb][fast][region]);
            returned(gba, identity, thumb != 0, MODE_SYS, false, addresses[region]);
            CHECK(cupid_timer_read_counter(gba, 0) == elapsed[thumb][fast][region]);
            destroy(gba);
            count++;
        }
      }
    }
    CHECK(count == 30);
}

static void changed_words_and_read_only_guest_access(void) {
    const unsigned addresses[] = {0x20, 0x3F00, 0x3FFC};
    const uint32_t changes[] = {0, 1, UINT32_MAX, 0x80000000};
    unsigned count = 0;
    for (unsigned thumb = 0; thumb < 2; thumb++) {
      for (unsigned a = 0; a < 3; a++) {
        for (unsigned change = 0; change < 4; change++) {
            struct CupidGBA* gba = machine(0);
            const uint32_t value = word(gba->memory.bios + addresses[a]) + changes[change];
            put_word(gba->memory.bios + addresses[a], value); /* Diagnostic image initialization. */
            cupid_memory_store32(&gba->cpu, addresses[a], ~value, NULL);
            CHECK(word(gba->memory.bios + addresses[a]) == value);
            prepare(gba, thumb != 0, MODE_SYS, true, GBA_BASE_IWRAM);
            CHECK(call(gba, thumb != 0, MODE_SYS, GBA_BASE_IWRAM) == 41011);
            returned(gba, identity + changes[change], thumb != 0, MODE_SYS, true, GBA_BASE_IWRAM);
            destroy(gba);
            count++;
        }
      }
    }
    CHECK(count == 24);
}

struct ChecksumDMA { struct CupidGBA* gba; bool dispatched; uint64_t when; uint32_t late; uint32_t cursor; };
static void request_dma(struct CupidTiming* timing, void* context, uint32_t late) {
    struct ChecksumDMA* trace = context;
    struct CupidGBA* gba = trace->gba;
    trace->dispatched = true;
    trace->when = cupid_timing_global_time(timing);
    trace->late = late;
    trace->cursor = gba->cpu.gprs[3];
    CHECK(timing->globalCycles == trace->when + late);
    cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM + 0x1000);
    cupid_dma_write_dad(gba, 3, GBA_BASE_EWRAM + 4);
    cupid_dma_write_cnt_lo(gba, 3, 1);
    cupid_dma_write_cnt(gba, 3, 0xC400);
}

static void install_handler(struct CupidGBA* gba) {
    const uint32_t handler[] = {
        0xE3A00301, 0xE2800C02, 0xE1D010B2, 0xE1C010B2,
        0xE59F2018, 0xE5923000, 0xE2833001, 0xE5823000,
        0xE5923004, 0xE1833001, 0xE5823004, 0xE12FFF1E, GBA_BASE_EWRAM + 0x2B00
    };
    memcpy(gba->memory.iwram + 0x7000, handler, sizeof(handler));
    put_word(gba->memory.iwram + 0x7FFC, GBA_BASE_IWRAM + 0x7000);
}

struct ChecksumProgress {
    uint64_t elapsed, after;
    uint32_t registers[16], status, savedStatus, protectedWord, interrupts, requests, late, cursor;
    uint16_t timer, pending;
};
static struct ChecksumProgress builtinProgress[2][2][3];

static void timer_dma_irq_masks_and_callback_deadlines(void) {
    if (externalBios) {
        const char* requested = externalBios;
        externalBios = NULL;
        timer_dma_irq_masks_and_callback_deadlines();
        externalBios = requested;
    }
    const uint64_t clocks[] = {0, UINT64_C(0xFFFFFFF0), UINT64_C(0x1FFFFFFF0)};
    unsigned count = 0;
    for (unsigned thumb = 0; thumb < 2; thumb++) {
      for (unsigned phase = 0; phase < 2; phase++) {
        for (unsigned c = 0; c < 3; c++) {
            struct CupidGBA* gba = machine(clocks[c]);
            prepare(gba, thumb != 0, MODE_SYS, false, GBA_BASE_IWRAM);
            install_handler(gba);
            cupid_io_write(gba, REG_TM0CNT_H, 0);
            cupid_timing_tick(&gba->timing, 2);
            cupid_io_write(gba, REG_TM0CNT_L, 0xF800);
            cupid_io_write(gba, REG_TM0CNT_H, 0xC0);
            cupid_timing_tick(&gba->timing, 2);
            cupid_io_write(gba, REG_IE, IRQ_TIMER0 | IRQ_DMA3);
            cupid_io_write(gba, REG_IME, 1);
            const uint64_t start = cupid_timing_global_time(&gba->timing);
            const uint32_t marker = 0x2468ACE0;
            put_word(gba->memory.iwram + 0x1000, marker);
            struct ChecksumDMA trace = {.gba = gba};
            struct CupidTimingEvent event = {.context = &trace, .callback = request_dma, .priority = 0x20};
            cupid_timing_schedule(&gba->timing, &event, phase ? 1000 : -1);
            const uint64_t elapsed = call(gba, thumb != 0, MODE_SYS, GBA_BASE_IWRAM);
            CHECK(trace.dispatched && trace.when == start + (phase ? 1000u : 0u) - (phase ? 0u : 1u));
            if (!phase) CHECK(trace.late > 0);
            else CHECK(trace.cursor > 0 && trace.cursor < GBA_SIZE_BIOS);
            CHECK(word(gba->memory.ewram + 4) == marker);
            CHECK(!gba->memory.dmaRunning && !gba->memory.dma[3].enable);
            CHECK(word(gba->memory.ewram + 0x2B00) == (phase ? 0u : 1u));
            returned(gba, identity, thumb != 0, MODE_SYS, false, GBA_BASE_IWRAM);
            CHECK(gba->irqf == (phase ? IRQ_TIMER0 | IRQ_DMA3 : IRQ_TIMER0));
            CHECK(cupid_timer_read_counter(gba, 0) == 0xF800 + elapsed % 2048);
            /* Masked timer and DMA requests are delivered after SWI return. */
            unsigned steps;
            for (steps = 0; steps < 500; steps++) {
                cupid_gba_step(gba);
                if (word(gba->memory.ewram + 0x2B00) == (phase ? 1u : 2u) &&
                    gba->cpu.privilegeMode == MODE_SYS && gba->cpu.executionMode == (thumb ? MODE_THUMB : MODE_ARM)) break;
            }
            CHECK(steps < 500);
            CHECK(word(gba->memory.ewram + 0x2B04) == (IRQ_TIMER0 | IRQ_DMA3));
            CHECK(!gba->irqf);
            struct ChecksumProgress result = {0};
            result.elapsed = elapsed;
            result.after = cupid_timing_global_time(&gba->timing) - start;
            memcpy(result.registers, gba->cpu.gprs, sizeof(result.registers));
            result.status = gba->cpu.cpsr.packed;
            result.savedStatus = gba->cpu.spsr.packed;
            result.protectedWord = gba->memory.biosPrefetch;
            result.interrupts = word(gba->memory.ewram + 0x2B00);
            result.requests = word(gba->memory.ewram + 0x2B04);
            result.late = trace.late;
            result.cursor = trace.cursor;
            result.timer = cupid_timer_read_counter(gba, 0);
            result.pending = gba->irqf;
            if (externalBios) {
                const struct ChecksumProgress* expected = &builtinProgress[thumb][phase][c];
                CHECK(result.elapsed == expected->elapsed && result.after == expected->after);
                CHECK(!memcmp(result.registers, expected->registers, sizeof(result.registers)));
                CHECK(result.status == expected->status && result.savedStatus == expected->savedStatus);
                CHECK(result.protectedWord == expected->protectedWord && result.timer == expected->timer);
                CHECK(result.interrupts == expected->interrupts && result.requests == expected->requests);
                CHECK(result.late == expected->late && result.cursor == expected->cursor && result.pending == expected->pending);
            } else builtinProgress[thumb][phase][c] = result;
            destroy(gba);
            count++;
        }
      }
    }
    CHECK(count == 12);
}

int main(int argc, char** argv) {
    int bank = -1, source = -1;
    bool words = false, interactions = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--external-bios") && i + 1 < argc) externalBios = argv[++i];
        else if (!strcmp(argv[i], "--bank") && i + 1 < argc) bank = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--source") && i + 1 < argc) source = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--words")) words = true;
        else if (!strcmp(argv[i], "--interactions")) interactions = true;
        else return 2;
    }
    if (bank >= 0) caller_bank((unsigned)bank);
    else if (source >= 0) caller_sources((unsigned)source);
    else if (words) changed_words_and_read_only_guest_access();
    else if (interactions) timer_dma_irq_masks_and_callback_deadlines();
    else return 2;
    puts("PASS complete checksum group");
    return 0;
}
