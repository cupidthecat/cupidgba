/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

void cupid_test_user_bank_gpio(const char* directory, unsigned window);

static const uint32_t codeAddress = GBA_BASE_IWRAM + 0x1000;
static const uint32_t dataAddress = GBA_BASE_IWRAM + 0x2000;
static const uint32_t nop = 0xE1A00000;

static struct CupidGBA* machine(unsigned mode, uint64_t clock) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
    gba->audio.lastSample = gba->audio.sampleOrigin = clock;
    gba->audio.nextSample = clock + 512;
    cupid_arm_set_priv_mode(&gba->cpu, mode);
    gba->cpu.cpsr.packed = mode | 0xC0;
    gba->cpu.irqDisableLatch = true;
    return gba;
}

static void destroy(struct CupidGBA* gba) { cupid_gba_destroy(gba); free(gba); }

static void program(struct CupidGBA* gba, uint32_t load, uint32_t following, uint32_t last) {
    const uint32_t instructions[] = {load, following, last, 0xE1A00000, 0xEAFFFFFE};
    memcpy(gba->memory.iwram + 0x1000, instructions, sizeof(instructions));
    gba->cpu.gprs[0] = dataAddress;
    gba->cpu.gprs[ARM_PC] = codeAddress;
    CupidARMWritePC(&gba->cpu);
}

static bool separate(unsigned mode, unsigned reg) {
    return mode != MODE_USR && mode != MODE_SYS
        && (reg >= 13 || (mode == MODE_FIQ && reg >= 8));
}

static uint32_t user_value(struct CupidGBA* gba, unsigned reg) {
    cupid_arm_set_priv_mode(&gba->cpu, MODE_SYS);
    return (uint32_t)gba->cpu.gprs[reg];
}

static void complete_register_matrix(void) {
    const unsigned modes[] = {MODE_USR, MODE_SYS, MODE_FIQ, MODE_IRQ, MODE_SVC, MODE_ABT, MODE_UND};
    const uint32_t values[] = {0, UINT32_MAX, 0xFFFF, 0xFFFF0000, 0x80000000, 0x7FFFFFFF, 0xA55A36C9, 0x5AA5C639};
    unsigned count = 0;
    for (unsigned m = 0; m < 7; ++m) for (unsigned reg = 0; reg < 15; ++reg)
      for (unsigned p = 0; p < 8; ++p) {
        struct CupidGBA* gba = machine(modes[m], 0);
        uint32_t loaded = values[p], original = values[(p + 3) & 7];
        gba->cpu.gprs[reg] = original;
        memcpy(gba->memory.iwram + 0x2000, &loaded, 4);
        program(gba, 0xE8D00000 | (1u << reg), 0xE1A01000 | reg, 0xE1A02000 | reg);
        cupid_gba_step(gba);
        bool banked = separate(modes[m], reg);
        CHECK((uint32_t)gba->cpu.gprs[reg] == (banked ? original : loaded));
        cupid_gba_step(gba);
        CHECK((uint32_t)gba->cpu.gprs[1] == (banked ? original | loaded : loaded));
        cupid_gba_step(gba);
        CHECK((uint32_t)gba->cpu.gprs[2] == (banked ? original : loaded));
        CHECK(user_value(gba, reg) == loaded);
        destroy(gba);
        ++count;
    }
    CHECK(count == 840);
    printf("PASS user-bank register matrix: %u complete transfers, all seven modes and R0-R14\n", count);
}

static struct CupidGBA* arithmetic_machine(uint32_t following) {
    struct CupidGBA* gba = machine(MODE_FIQ, 0);
    const uint32_t input[] = {0x2481, 0x4218, 0xF0, 1};
    memcpy(gba->memory.iwram + 0x2000, input, sizeof(input));
    gba->cpu.gprs[8] = 0x1248;
    gba->cpu.gprs[9] = 0x8421;
    gba->cpu.gprs[10] = 0xF;
    gba->cpu.gprs[11] = 0;
    program(gba, 0xE8D00F00, following, nop);
    cupid_gba_step(gba);
    cupid_gba_step(gba);
    return gba;
}

static void arithmetic_operands_and_writeback(void) {
    struct CupidGBA* gba = arithmetic_machine(0xE088A009); /* ADD r10,r8,r9 */
    CHECK(gba->cpu.gprs[10] == 0xFD02);
    CHECK(user_value(gba, 10) == 0xF0);
    destroy(gba);
    gba = arithmetic_machine(0xE0020998); /* MUL r2,r8,r9 */
    CHECK((uint32_t)gba->cpu.gprs[2] == UINT32_C(0x36C9) * 0xC639);
    destroy(gba);
    gba = arithmetic_machine(0xE022A998); /* MLA r2,r8,r9,r10 */
    CHECK((uint32_t)gba->cpu.gprs[2] == UINT32_C(0x1248) * 0x8421 + 0xFF);
    destroy(gba);
    gba = arithmetic_machine(0xE0882B19); /* ADD r2,r8,r9,LSL r11 */
    CHECK(gba->cpu.gprs[2] == 0x1248 + (0x8421 << 1));
    destroy(gba);
    for (unsigned sign = 0; sign < 2; ++sign) {
        gba = machine(MODE_FIQ, 0);
        const uint32_t input[] = {0x1111,0x1111,0x11111111,0x11111111};
        memcpy(gba->memory.iwram + 0x2000, input, sizeof(input));
        gba->cpu.gprs[8] = gba->cpu.gprs[9] = 0x11110000;
        gba->cpu.gprs[10] = gba->cpu.gprs[11] = 0x01010101;
        program(gba, 0xE8D00F00, sign ? 0xE0E98B9A : 0xE0A98B9A, nop);
        cupid_gba_step(gba); cupid_gba_step(gba);
        uint64_t expected = UINT64_C(0x01010101) * 0x01010101 + UINT64_C(0x1111111111111111);
        CHECK((uint32_t)gba->cpu.gprs[8] == (uint32_t)expected);
        CHECK((uint32_t)gba->cpu.gprs[9] == (uint32_t)(expected >> 32));
        CHECK(user_value(gba, 8) == 0x1111 && gba->cpu.gprs[9] == 0x1111);
        destroy(gba);
    }
    /* Flags come from the ALU result, not a second conflicted register read. */
    gba = machine(MODE_FIQ, 0);
    uint32_t input = 0xFFFF;
    memcpy(gba->memory.iwram + 0x2000, &input, 4);
    gba->cpu.gprs[8] = (int32_t)0xFFFF0000;
    program(gba, 0xE8D00100, 0xE2988001, nop); /* ADDS r8,r8,#1 */
    cupid_gba_step(gba); cupid_gba_step(gba);
    CHECK(gba->cpu.gprs[8] == 0 && gba->cpu.cpsr.z && gba->cpu.cpsr.c);
    CHECK(!gba->cpu.cpsr.n && !gba->cpu.cpsr.v);
    CHECK(user_value(gba, 8) == 0xFFFF);
    destroy(gba);
}

static void memory_address_data_and_swap_phases(void) {
    const uint32_t operations[] = {0xE5982000,0xE5889000,0xE108A099}; /* LDR, STR, SWP */
    for (unsigned op = 0; op < 3; ++op) {
        struct CupidGBA* gba = machine(MODE_FIQ, 0);
        const uint32_t input[] = {4,8,12};
        const uint32_t array[] = {1,2,0xDEADBEEF};
        memcpy(gba->memory.iwram + 0x2000, input, sizeof(input));
        memcpy(gba->memory.iwram + 0x3000, array, sizeof(array));
        gba->cpu.gprs[8] = GBA_BASE_IWRAM + 0x3000;
        gba->cpu.gprs[9] = 3;
        gba->cpu.gprs[10] = (int32_t)0xA5A5A5A5;
        program(gba, 0xE8D00700, operations[op], nop);
        cupid_gba_step(gba); cupid_gba_step(gba);
        uint32_t result[3]; memcpy(result, gba->memory.iwram + 0x3000, sizeof(result));
        CHECK(result[0] == 1 && result[2] == 0xDEADBEEF);
        if (!op) CHECK(gba->cpu.gprs[2] == 2 && result[1] == 2);
        else if (op == 1) CHECK(result[1] == 11);
        else CHECK(result[1] == 3 && gba->cpu.gprs[10] == 2);
        CHECK(user_value(gba, 10) == 12);
        destroy(gba);
    }
    /* Post-indexed writeback retains both early address operands after the
     * data phase releases the register-port conflict. */
    const uint32_t indexed[] = {0xE4982008,0xE6982009,0xE5B82008};
    for (unsigned op = 0; op < 3; ++op) {
        struct CupidGBA* gba = machine(MODE_FIQ, 0);
        const uint32_t input[] = {4,8};
        const uint32_t array[] = {1,2,3,4};
        memcpy(gba->memory.iwram + 0x2000, input, sizeof(input));
        memcpy(gba->memory.iwram + 0x3000, array, sizeof(array));
        gba->cpu.gprs[8] = GBA_BASE_IWRAM + 0x3000;
        gba->cpu.gprs[9] = 0;
        program(gba, 0xE8D00300, indexed[op], nop);
        cupid_gba_step(gba); cupid_gba_step(gba);
        CHECK(gba->cpu.gprs[2] == (op == 2 ? 4 : 2));
        CHECK((uint32_t)gba->cpu.gprs[8] == GBA_BASE_IWRAM + 0x300C);
        CHECK(user_value(gba, 8) == 4);
        destroy(gba);
    }
}

static void predicates_branches_reset_and_exceptions(void) {
    struct CupidGBA* gba = machine(MODE_FIQ, 0);
    uint32_t input = 1;
    memcpy(gba->memory.iwram + 0x2000, &input, 4);
    gba->cpu.gprs[8] = codeAddress + 0x100;
    uint16_t thumb[] = {0x225A, 0xE7FE};
    memcpy(gba->memory.iwram + 0x1100, thumb, sizeof(thumb));
    program(gba, 0xE8D00100, 0xE12FFF18, nop); /* BX r8 takes the conflicted low bit. */
    cupid_gba_step(gba); cupid_gba_step(gba);
    CHECK(gba->cpu.executionMode == MODE_THUMB && gba->cpu.gprs[ARM_PC] == (int32_t)(codeAddress + 0x102));
    cupid_gba_step(gba);
    CHECK(gba->cpu.gprs[2] == 0x5A);
    destroy(gba);
    gba = machine(MODE_FIQ, 0);
    input = 0x2222; memcpy(gba->memory.iwram + 0x2000, &input, 4);
    gba->cpu.gprs[8] = 0x1111;
    gba->cpu.cpsr.z = 1;
    program(gba, 0xE8D00100, 0x11A01008, 0xE1A02008); /* Failed MOVNE consumes the phase. */
    cupid_gba_step(gba); cupid_gba_step(gba); cupid_gba_step(gba);
    CHECK(gba->cpu.gprs[1] == 0 && gba->cpu.gprs[2] == 0x1111);
    destroy(gba);
    gba = machine(MODE_FIQ, 0);
    memcpy(gba->memory.iwram + 0x2000, &input, 4);
    program(gba, 0xE8D04000, nop, nop);
    cupid_gba_step(gba);
    const uint32_t vector[] = {0xE1A0100E,nop,nop}; /* MOV r1,lr */
    memcpy(gba->memory.bios + BASE_IRQ, vector, sizeof(vector));
    gba->cpu.cpsr.i = 0;
    cupid_arm_raise_irq(&gba->cpu);
    uint32_t link = (uint32_t)gba->cpu.gprs[ARM_LR];
    cupid_gba_step(gba);
    CHECK((uint32_t)gba->cpu.gprs[1] == link && gba->cpu.privilegeMode == MODE_IRQ);
    cupid_gba_reset(gba);
    CHECK(gba->cpu.privilegeMode == MODE_SYS && gba->cpu.gprs[1] == 0);
    destroy(gba);
    /* STM^ and ordinary LDM do not create the following read conflict. */
    for (unsigned store = 0; store < 2; ++store) {
        gba = machine(MODE_FIQ, 0);
        gba->cpu.gprs[8] = 0x1234;
        gba->cpu.bankedRegisters[BANK_NONE][2] = 0xF0;
        input = 0x55; memcpy(gba->memory.iwram + 0x2000, &input, 4);
        program(gba, store ? 0xE8C00100 : 0xE8900100, 0xE1A01008, nop);
        cupid_gba_step(gba); cupid_gba_step(gba);
        CHECK(gba->cpu.gprs[1] == (store ? 0x1234 : 0x55));
        CHECK(user_value(gba, 8) == 0xF0);
        destroy(gba);
    }
    /* Reset must clear a pending phase even without a later PC refill. */
    gba = machine(MODE_FIQ, 0);
    memcpy(gba->memory.iwram + 0x2000, &input, 4);
    program(gba, 0xE8D00100, nop, nop);
    cupid_gba_step(gba);
    cupid_gba_reset(gba);
    cupid_arm_set_priv_mode(&gba->cpu, MODE_FIQ);
    gba->cpu.cpsr.packed = MODE_FIQ | 0xC0;
    gba->cpu.gprs[8] = 0x1234;
    gba->cpu.bankedRegisters[BANK_NONE][2] = 0x80000000;
    gba->cpu.gprs[ARM_PC] = codeAddress + 4;
    gba->cpu.prefetch[0] = 0xE1A01008;
    gba->cpu.prefetch[1] = nop;
    cupid_memory_set_active_region(&gba->cpu, codeAddress);
    cupid_gba_step(gba);
    CHECK(gba->cpu.gprs[1] == 0x1234);
    destroy(gba);
}

static void dma_stalls_timer_requests_and_clock_wrap(void) {
    const uint64_t origins[] = {0, UINT32_MAX - 12u, UINT64_C(0x300000000)};
    for (unsigned origin = 0; origin < 3; ++origin) {
        struct CupidGBA* gba = machine(MODE_FIQ, origins[origin]);
        const uint32_t input = 0x2222;
        memcpy(gba->memory.iwram + 0x2000, &input, 4);
        gba->cpu.gprs[8] = 0x1111;
        program(gba, 0xE8D00100, 0xE1A01008, 0xE1A02008);
        cupid_gba_step(gba);
        /* Dispatch the load's accumulated final clock before direct board ticks. */
        gba->cpu.nextEvent = gba->cpu.cycles;
        gba->cpu.irqh.processEvents(&gba->cpu);
        CHECK(!gba->cpu.cycles);
        for (unsigned i = 0; i < 256; ++i) gba->memory.iwram[0x3000 + i] = (uint8_t)(i ^ 0xA5);
        cupid_io_write(gba, REG_TM0CNT_L, 0xFFF8);
        cupid_io_write(gba, REG_TM0CNT_H, 0xC0);
        cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM + 0x3000);
        cupid_dma_write_dad(gba, 3, GBA_BASE_EWRAM + 0x1000);
        cupid_dma_write_cnt_lo(gba, 3, 128);
        cupid_dma_write_cnt(gba, 3, 0xC000);
        /* Establish DMA ownership before issuing the next CPU operand phase. */
        cupid_timing_tick(&gba->timing, 2);
        CHECK(gba->memory.dmaRunning);
        uint64_t start = cupid_timing_global_time(&gba->timing);
        cupid_gba_step(gba); cupid_gba_step(gba);
        CHECK(cupid_timing_global_time(&gba->timing) > start + 128);
        CHECK(gba->cpu.gprs[1] == 0x3333 && gba->cpu.gprs[2] == 0x1111);
        CHECK((gba->irq.pendingIF & (IRQ_TIMER0 | IRQ_DMA3)) == (IRQ_TIMER0 | IRQ_DMA3));
        CHECK(!gba->memory.dmaRunning && !gba->memory.dma[3].enable);
        for (unsigned i = 0; i < 256; ++i) CHECK(gba->memory.ewram[0x1000 + i] == (uint8_t)(i ^ 0xA5));
        destroy(gba);
    }
}

int cupid_test_user_bank_probe(int argc, char** argv);
void cupid_test_user_bank_observer(void);

int main(int argc, char** argv) {
    if (argc >= 2 && !strcmp(argv[1], "probe")) return cupid_test_user_bank_probe(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "gpio")) {
        CHECK(argc == 4 && argv[3][0] >= '0' && argv[3][0] <= '2' && !argv[3][1]);
        cupid_test_user_bank_gpio(argv[2], (unsigned)(argv[3][0] - '0'));
        return 0;
    }
    CHECK(argc == 2);
    if (!strcmp(argv[1], "registers")) complete_register_matrix();
    else if (!strcmp(argv[1], "arithmetic")) RUN(arithmetic_operands_and_writeback);
    else if (!strcmp(argv[1], "memory")) RUN(memory_address_data_and_swap_phases);
    else if (!strcmp(argv[1], "control")) RUN(predicates_branches_reset_and_exceptions);
    else if (!strcmp(argv[1], "chronology")) RUN(dma_stalls_timer_requests_and_clock_wrap);
    else if (!strcmp(argv[1], "observer")) cupid_test_user_bank_observer();
    else CHECK(false);
    return 0;
}
