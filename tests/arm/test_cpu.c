/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

static void complete_dispatch_tables(void) {
    for (unsigned i = 0; i < 4096; i++) CHECK(cupid_arm_table[i] != NULL);
    for (unsigned i = 0; i < 1024; i++) CHECK(cupid_thumb_table[i] != NULL);
}

static void swi_decode_and_exception(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba != NULL);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    gba->biosHLE = false;
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
    uint32_t swi = 0xEF060000;
    memcpy(gba->memory.iwram, &swi, sizeof(swi));
    CupidARMWritePC(&gba->cpu);
    union CupidPSR previous = gba->cpu.cpsr;
    cupid_arm_step(&gba->cpu);
    CHECK(gba->cpu.privilegeMode == MODE_SVC);
    CHECK(gba->cpu.spsr.packed == previous.packed);
    CHECK((uint32_t)gba->cpu.gprs[ARM_LR] == GBA_BASE_IWRAM + 4);
    CHECK(gba->cpu.gprs[ARM_PC] == BASE_SWI + 4 && gba->cpu.cpsr.i);
    cupid_gba_destroy(gba);
    free(gba);
}

static void execute(struct CupidGBA* gba, uint32_t opcode, bool thumb) {
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    memcpy(gba->memory.iwram, &opcode, sizeof(opcode));
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
    if (thumb) { CupidThumbWritePC(&gba->cpu); cupid_thumb_step(&gba->cpu); }
    else { CupidARMWritePC(&gba->cpu); cupid_arm_step(&gba->cpu); }
}

static void fixed_width_arithmetic(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba != NULL);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    gba->cpu.gprs[0] = INT32_MAX;
    execute(gba, 0xE2900001, false); /* ADDS r0, r0, #1 */
    CHECK((uint32_t)gba->cpu.gprs[0] == 0x80000000u);
    CHECK(gba->cpu.cpsr.n && gba->cpu.cpsr.v && !gba->cpu.cpsr.c && !gba->cpu.cpsr.z);
    gba->cpu.gprs[0] = -1;
    execute(gba, 0xE2900001, false);
    CHECK(gba->cpu.gprs[0] == 0 && gba->cpu.cpsr.c && gba->cpu.cpsr.z && !gba->cpu.cpsr.v);

    gba->cpu.gprs[1] = INT32_MIN;
    gba->cpu.cpsr.c = 0;
    execute(gba, 0xE1B00041, false); /* MOVS r0, r1, ASR #32 */
    CHECK(gba->cpu.gprs[0] == -1 && gba->cpu.cpsr.c && gba->cpu.cpsr.n);
    gba->cpu.gprs[1] = 1;
    gba->cpu.cpsr.c = 1;
    execute(gba, 0xE1B00041, false);
    CHECK(gba->cpu.gprs[0] == 0 && !gba->cpu.cpsr.c && gba->cpu.cpsr.z);

    gba->cpu.gprs[0] = -1;
    execute(gba, 0x3001, true); /* ADDS r0, #1 */
    CHECK(gba->cpu.gprs[0] == 0 && gba->cpu.cpsr.c && gba->cpu.cpsr.z);
    gba->cpu.gprs[0] = 1;
    execute(gba, 0x07C0, true); /* LSLS r0, r0, #31 */
    CHECK((uint32_t)gba->cpu.gprs[0] == 0x80000000u && gba->cpu.cpsr.n);
    cupid_gba_destroy(gba);
    free(gba);
}

static void thumb_store_base_in_list(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba != NULL);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    gba->cpu.gprs[0] = 0x12345678;
    gba->cpu.gprs[1] = GBA_BASE_EWRAM + 1; /* Preserve low address bits in writeback. */
    execute(gba, 0xC103, true); /* STMIA r1!, {r0, r1} */
    CHECK((uint32_t)gba->cpu.gprs[1] == GBA_BASE_EWRAM + 9);
    CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_EWRAM, NULL) == 0x12345678);
    CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_EWRAM + 4, NULL) == GBA_BASE_EWRAM + 9);
    gba->cpu.gprs[1] = GBA_BASE_EWRAM + 16;
    execute(gba, 0xC106, true); /* STMIA r1!, {r1, r2} */
    CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_EWRAM + 16, NULL) == GBA_BASE_EWRAM + 16);
    CHECK((uint32_t)gba->cpu.gprs[1] == GBA_BASE_EWRAM + 24);
    cupid_gba_destroy(gba);
    free(gba);
}

static void empty_multiple_loads_pc(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba != NULL);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_memory_store32(&gba->cpu, GBA_BASE_EWRAM, GBA_BASE_IWRAM + 64, NULL);
    gba->cpu.gprs[0] = GBA_BASE_EWRAM + 1;
    execute(gba, 0xC800, true); /* LDMIA r0!, {} */
    CHECK((uint32_t)gba->cpu.gprs[0] == GBA_BASE_EWRAM + 65);
    CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == GBA_BASE_IWRAM + 66);
    for (int direction = LSM_IA; direction <= LSM_DB; direction++) {
        uint32_t base = GBA_BASE_EWRAM + 256;
        uint32_t transfer = base;
        if (direction & LSM_D) transfer -= 64;
        if ((direction & LSM_B) != ((direction & LSM_D) >> 1)) transfer += 4;
        cupid_memory_store32(&gba->cpu, transfer, 0x12345678, NULL);
        uint32_t result = cupid_memory_load_multiple(&gba->cpu, base + 1, 0,
                                                    (enum CupidLSMDirection)direction, NULL);
        CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == 0x12345678);
        CHECK(result == (direction & LSM_D ? base - 63 : base + 65));
    }
    cupid_gba_destroy(gba);
    free(gba);
}

static void arm_pc_alignment(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba != NULL);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 2;
    CupidARMWritePC(&gba->cpu);
    CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == GBA_BASE_IWRAM + 4);
    cupid_gba_destroy(gba);
    free(gba);
}

static void block_transfers_charge_one_final_internal_cycle(void) {
    const uint32_t regions[] = {GBA_BASE_IWRAM + 0x100, GBA_BASE_EWRAM};
    const uint64_t loadCycles[] = {6, 26};
    const uint64_t storeCycles[] = {5, 25};
    for (unsigned i = 0; i < 2; i++) {
        struct CupidGBA* gba = calloc(1, sizeof(*gba));
        CHECK(gba);
        cupid_gba_create(gba);
        cupid_gba_reset(gba);
        const uint32_t values[] = {0x12345678, 0x9ABCDEF0, 0x13572468, 0xA5A5A5A5};
        uint8_t* data = i ? gba->memory.ewram : gba->memory.iwram + 0x100;
        memcpy(data, values, sizeof(values));
        gba->cpu.gprs[10] = regions[i];
        uint64_t before = cupid_timing_global_time(&gba->timing);
        execute(gba, 0xE8BA000F, false); /* ldmia r10!, {r0-r3} */
        CHECK(cupid_timing_global_time(&gba->timing) - before == loadCycles[i]);
        CHECK(!memcmp(gba->cpu.gprs, values, sizeof(values)));
        CHECK((uint32_t)gba->cpu.gprs[10] == regions[i] + sizeof(values));
        memset(data, 0, sizeof(values));
        gba->cpu.gprs[10] = regions[i];
        before = cupid_timing_global_time(&gba->timing);
        execute(gba, 0xE8AA000F, false); /* stmia r10!, {r0-r3} */
        CHECK(cupid_timing_global_time(&gba->timing) - before == storeCycles[i]);
        CHECK(!memcmp(data, values, sizeof(values)));
        cupid_gba_destroy(gba);
        free(gba);
    }
}

int main(void) {
    RUN(block_transfers_charge_one_final_internal_cycle);
    RUN(complete_dispatch_tables);
    RUN(swi_decode_and_exception);
    RUN(fixed_width_arithmetic);
    RUN(thumb_store_base_in_list);
    RUN(empty_multiple_loads_pc);
    RUN(arm_pc_alignment);
    return 0;
}
