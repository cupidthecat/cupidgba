/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

enum { CALLER = GBA_BASE_IWRAM + 0x100, HANDLER = GBA_BASE_IWRAM + 0x400 };
static const enum CupidPrivMode modes[] = {MODE_USR, MODE_FIQ, MODE_IRQ, MODE_SVC, MODE_ABT, MODE_UND, MODE_SYS};
static const uint64_t epochs[] = {0, UINT32_MAX - 8u, UINT64_C(0x300000000)};
static unsigned observations;
int cupid_test_irq_source_probe(int argc, char** argv);
void cupid_test_irq_source_observer(void);
struct Record {
    unsigned calls, entry, restore;
    uint32_t link, stack, frame[6];
};
struct Banks { int32_t registers[6][7], saved[6]; uint32_t activeSaved; };
static enum CupidRegisterBank bank(enum CupidPrivMode mode) {
    switch (mode) {
    case MODE_FIQ: return BANK_FIQ; case MODE_IRQ: return BANK_IRQ;
    case MODE_SVC: return BANK_SUPERVISOR; case MODE_ABT: return BANK_ABORT;
    case MODE_UND: return BANK_UNDEFINED; default: return BANK_NONE;
    }
}
static struct Banks banks(const struct CupidARM* cpu) {
    struct Banks result;
    memcpy(result.registers, cpu->bankedRegisters, sizeof(result.registers));
    memcpy(result.saved, cpu->bankedSPSRs, sizeof(result.saved));
    enum CupidRegisterBank active = bank(cpu->privilegeMode);
    /* Normalize cached slots into current register-file values. This reads
     * the bank state without switching modes or clocking the core. */
    result.registers[active][0] = cpu->gprs[ARM_SP];
    result.registers[active][1] = cpu->gprs[ARM_LR];
    enum CupidRegisterBank high = active == BANK_FIQ ? BANK_FIQ : BANK_NONE;
    for (unsigned r = 8; r <= 12; ++r) result.registers[high][r - 6] = cpu->gprs[r];
    result.saved[active] = cpu->spsr.packed;
    result.activeSaved = (uint32_t)cpu->spsr.packed;
    return result;
}
static void observe(const struct CupidARM* cpu, void* context) {
    struct Record* record = context;
    uint32_t address = cupid_arm_instruction_address(cpu);
    if (cpu->executionMode != MODE_ARM) return;
    if (address == 0x128 && cpu->privilegeMode == MODE_IRQ) ++record->entry;
    if (address == 0x138 && cpu->privilegeMode == MODE_IRQ) ++record->restore;
    if (address != HANDLER) return;
    ++record->calls;
    record->link = (uint32_t)cpu->gprs[ARM_LR];
    record->stack = (uint32_t)cpu->gprs[ARM_SP];
    CHECK(record->stack >= GBA_BASE_IWRAM && record->stack <= GBA_BASE_IWRAM + GBA_SIZE_IWRAM - 24);
    memcpy(record->frame, cpu->master->memory.iwram + (record->stack & 0x7FFF), sizeof(record->frame));
}
static struct CupidGBA* machine(const char* bios) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    if (bios) CHECK(cupid_load_bios(gba, bios));
    return gba;
}
static void prepare(struct CupidGBA* gba, enum CupidPrivMode mode, unsigned flags,
                    bool fiqMask, bool thumb, bool timed, bool literal, uint64_t epoch) {
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = epoch;
    gba->timing.masterCycles = (uint32_t)epoch;
    cupid_arm_set_priv_mode(&gba->cpu, MODE_SYS);
    for (unsigned r = 8; r <= 12; ++r) gba->cpu.gprs[r] = (int32_t)(0x24680000u + r);
    cupid_arm_set_priv_mode(&gba->cpu, MODE_IRQ);
    gba->cpu.gprs[ARM_SP] = GBA_SP_BASE_IRQ;
    cupid_arm_set_priv_mode(&gba->cpu, mode);
    for (unsigned r = 0; r < 15; ++r) gba->cpu.gprs[r] = (int32_t)(0x12340000u + (unsigned)mode * 256u + r);
    gba->cpu.gprs[4] = GBA_BASE_EWRAM + 0x100;
    gba->cpu.gprs[5] = GBA_BASE_IO + REG_IME;
    gba->cpu.gprs[6] = 0;
    gba->cpu.gprs[ARM_SP] = mode == MODE_IRQ ? GBA_SP_BASE_IRQ : GBA_SP_BASE_SYSTEM;
    gba->cpu.cpsr.packed = (int32_t)(flags << 28 | (fiqMask ? 0x40u : 0) | mode);
    gba->cpu.irqDisableLatch = false;
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    const uint32_t arm[] = {0xEAFFFFFE, 0xE1A00000, 0xE1A00000};
    const uint16_t narrow[] = {0xE7FE, 0x46C0, 0x46C0};
    const uint32_t handler[] = {0xE584E000, 0xE5856000, literal ? 0xE51FF004u : 0xE12FFF1Eu, 0x138, 0xE1A00000};
    const uint32_t pointer = HANDLER;
    memcpy(gba->memory.iwram + 0x100, thumb ? (const void*)narrow : (const void*)arm, thumb ? sizeof(narrow) : sizeof(arm));
    memcpy(gba->memory.iwram + 0x400, handler, sizeof(handler));
    memcpy(gba->memory.iwram + 0x7FFC, &pointer, 4);
    gba->cpu.gprs[ARM_PC] = CALLER;
    if (thumb) CupidThumbWritePC(&gba->cpu); else CupidARMWritePC(&gba->cpu);
    gba->cpu.cycles = 0;
    if (!timed) gba->cpu.memory.fetch = NULL;
}
static void run_case(struct CupidGBA* gba, enum CupidPrivMode mode, bool thumb, bool peripherals, bool halt) {
    int32_t before[15];
    memcpy(before, gba->cpu.gprs, sizeof(before));
    uint32_t cpsr = (uint32_t)gba->cpu.cpsr.packed;
    struct Banks expectedBanks = banks(&gba->cpu);
    struct Record record = {0};
    gba->cpu.instructionObserver = observe;
    gba->cpu.instructionObserverContext = &record;
    cupid_io_write(gba, REG_IE, IRQ_TIMER0);
    cupid_io_write(gba, REG_IME, 1);
    cupid_io_write(gba, REG_TM0CNT_L, 0xFFFF);
    cupid_io_write(gba, REG_TM0CNT_H, 0xC0);
    const uint32_t payload = 0xA57B4E91;
    if (peripherals) {
        cupid_timing_tick(&gba->timing, 2);
        memcpy(gba->memory.iwram + 0x1000, &payload, 4);
        cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM + 0x1000);
        cupid_dma_write_dad(gba, 3, GBA_BASE_EWRAM + 0x2000);
        cupid_dma_write_cnt_lo(gba, 3, 1);
        cupid_dma_write_cnt(gba, 3, 0xC400);
        cupid_timing_tick(&gba->timing, 2);
        if (!gba->cpu.memory.fetch) cupid_bus_wait(&gba->cpu);
    }
    if (halt) gba->cpu.halted = 1;
    bool returned = false;
    for (unsigned steps = 0; steps < 1000; ++steps) {
        cupid_gba_step(gba);
        if (record.restore && gba->cpu.privilegeMode == mode
            && gba->cpu.executionMode == (thumb ? MODE_THUMB : MODE_ARM)
            && cupid_arm_instruction_address(&gba->cpu) == CALLER) { returned = true; break; }
    }
    CHECK(record.entry == 1 && record.calls == 1 && record.restore == 1 && returned);
    CHECK(record.link == 0x138 && record.stack == GBA_SP_BASE_IRQ - 24);
    uint32_t memory;
    memcpy(&memory, gba->memory.ewram + 0x100, 4);
    CHECK(memory == 0x138 && (uint32_t)gba->cpu.cpsr.packed == cpsr);
    for (unsigned r = 0; r < 15; ++r) {
        /* An IRQ taken in IRQ mode overwrites that mode's LR architecturally.
         * The wrapper restores the value captured on exception entry. */
        uint32_t expected = mode == MODE_IRQ && r == ARM_LR ? CALLER + 4u : (uint32_t)before[r];
        CHECK((uint32_t)gba->cpu.gprs[r] == expected);
    }
    for (unsigned r = 0; r < 4; ++r) CHECK(record.frame[r] == (uint32_t)before[r]);
    CHECK(record.frame[4] == (mode == MODE_FIQ ? 0x2468000Cu : (uint32_t)before[12]));
    CHECK(record.frame[5] == CALLER + 4u);
    struct Banks afterBanks = banks(&gba->cpu);
    expectedBanks.registers[BANK_IRQ][0] = GBA_SP_BASE_IRQ;
    expectedBanks.registers[BANK_IRQ][1] = CALLER + 4;
    expectedBanks.saved[BANK_IRQ] = (int32_t)cpsr;
    if (mode == MODE_IRQ) expectedBanks.activeSaved = cpsr;
    CHECK(!memcmp(expectedBanks.registers, afterBanks.registers, sizeof(expectedBanks.registers)));
    CHECK(!memcmp(expectedBanks.saved, afterBanks.saved, sizeof(expectedBanks.saved)));
    CHECK(expectedBanks.activeSaved == afterBanks.activeSaved);
    CHECK(gba->memory.biosPrefetch == 0xE55EC002);
    int32_t readCycles = 0;
    CHECK(gba->cpu.memory.load32(&gba->cpu, 0, &readCycles) == 0xE55EC002);
    if (peripherals) {
        CHECK(!gba->memory.dmaRunning && !gba->memory.dma[3].enable);
        CHECK(!memcmp(gba->memory.ewram + 0x2000, &payload, 4));
        CHECK((gba->irqf & (IRQ_TIMER0 | IRQ_DMA3)) == (IRQ_TIMER0 | IRQ_DMA3));
    }
    gba->cpu.instructionObserver = NULL;
    gba->cpu.instructionObserverContext = NULL;
    ++observations;
}
static void context(const char* bios, bool thumb) {
    for (unsigned timed = 0; timed < 2; ++timed) {
        struct CupidGBA* gba = machine(bios);
        for (unsigned epoch = 0; epoch < 3; ++epoch) for (unsigned m = 0; m < 7; ++m)
          for (unsigned flags = 0; flags < 16; ++flags) for (unsigned fiq = 0; fiq < 2; ++fiq)
            for (unsigned literal = 0; literal < 2; ++literal) {
                prepare(gba, modes[m], flags, fiq != 0, thumb, timed != 0, literal != 0, epochs[epoch]);
                run_case(gba, modes[m], thumb, false, false);
            }
        cupid_gba_destroy(gba); free(gba);
    }
    CHECK(observations == 2688);
}
static void events(const char* bios) {
    for (unsigned timed = 0; timed < 2; ++timed) {
        struct CupidGBA* gba = machine(bios);
        for (unsigned epoch = 0; epoch < 3; ++epoch) for (unsigned m = 0; m < 7; ++m)
          for (unsigned thumb = 0; thumb < 2; ++thumb) for (unsigned halt = 0; halt < 2; ++halt)
            for (unsigned literal = 0; literal < 2; ++literal) {
                prepare(gba, modes[m], 10, true, thumb != 0, timed != 0, literal != 0, epochs[epoch]);
                run_case(gba, modes[m], thumb != 0, true, halt != 0);
            }
        cupid_gba_destroy(gba); free(gba);
    }
    CHECK(observations == 336);
}
int main(int argc, char** argv) {
    if (argc >= 2 && !strcmp(argv[1], "probe")) return cupid_test_irq_source_probe(argc, argv);
    if (argc == 2 && !strcmp(argv[1], "observer")) { cupid_test_irq_source_observer(); return 0; }
    CHECK(argc == 2 || argc == 4);
    const char* bios = NULL;
    if (argc == 4) { CHECK(!strcmp(argv[2], "--bios")); bios = argv[3]; }
    if (!strcmp(argv[1], "arm")) context(bios, false);
    else if (!strcmp(argv[1], "thumb")) context(bios, true);
    else if (!strcmp(argv[1], "events")) events(bios);
    else CHECK(false);
    printf("Firmware IRQ observations: %u\n", observations);
    return 0;
}
