/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"
#include "../bios/caller.h"

static const uint32_t regions[] = {GBA_BASE_IWRAM, GBA_BASE_EWRAM, GBA_BASE_ROM0};
static const uint64_t clocks[] = {0, INT32_MAX - UINT64_C(4), UINT32_MAX - UINT64_C(4), UINT64_C(0x300000000)};

static struct CupidGBA* machine(uint64_t clock, bool prefetch) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba)); CHECK(gba);
    cupid_gba_create(gba); cupid_gba_reset(gba); cupid_test_bios_caller(gba);
    gba->memory.rom = calloc(1, 0x4000); CHECK(gba->memory.rom);
    gba->memory.romSize = gba->memory.romImageSize = 0x4000;
    gba->memory.romMask = 0x3FFF;
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock; gba->timing.masterCycles = (uint32_t)clock;
    gba->cpu.cpsr.i = 1; gba->cpu.irqDisableLatch = true;
    cupid_memory_update_waitstates(gba, prefetch ? 0x4000 : 0);
    return gba;
}
static void destroy(struct CupidGBA* gba) { cupid_gba_destroy(gba); free(gba); }
static uint8_t* code(struct CupidGBA* gba, unsigned region) {
    return (region == 0 ? gba->memory.iwram : region == 1 ? gba->memory.ewram : gba->memory.rom) + 0x1000;
}
static void start(struct CupidGBA* gba, uint32_t address, bool thumb) {
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = address;
    if (thumb) CupidThumbWritePC(&gba->cpu); else CupidARMWritePC(&gba->cpu);
}

static void refill_keeps_logical_a1_and_unrotated_words(void) {
    const uint32_t words[] = {0xE3A00017, 0xE3A01029, 0xE3A0203B, 0xEAFFFFFE};
    for (unsigned region = 0; region < 3; ++region) for (unsigned enabled = 0; enabled < 2; ++enabled)
    for (unsigned low = 0; low < 4; ++low) {
        struct CupidGBA* gba = machine(0, enabled != 0);
        memcpy(code(gba, region), words, sizeof(words));
        uint32_t base = regions[region] + 0x1000;
        start(gba, base + low, false);
        CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == base + (low & 2u) + 4);
        CHECK(cupid_arm_instruction_address(&gba->cpu) == base);
        CHECK(gba->cpu.prefetch[0] == words[0] && gba->cpu.prefetch[1] == words[1]);
        cupid_gba_step(gba);
        CHECK(gba->cpu.gprs[0] == 0x17 && gba->cpu.prefetch[0] == words[1] && gba->cpu.prefetch[1] == words[2]);
        CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == base + (low & 2u) + 8);
        cupid_gba_step(gba); CHECK(gba->cpu.gprs[1] == 0x29);
        destroy(gba);
    }
}

static void pc_operands_and_data_rotation_use_distinct_addresses(void) {
    /* LDR r0,[r1]; ADD r2,pc,#0; MOV r3,pc; STR r3,[r5]. */
    const uint32_t words[] = {0xE5910000, 0xE28F2000, 0xE1A0300F, 0xE5853000, 0xEAFFFFFE, 0xEAFFFFFE};
    for (unsigned region = 0; region < 3; ++region) for (unsigned low = 0; low <= 2; low += 2) {
        struct CupidGBA* gba = machine(0, true);
        memcpy(code(gba, region), words, sizeof(words));
        const uint32_t data = 0x11223344;
        memcpy(gba->memory.ewram + 0x2000, &data, 4);
        gba->cpu.gprs[1] = GBA_BASE_EWRAM + 0x2002; gba->cpu.gprs[5] = GBA_BASE_EWRAM + 0x2004;
        uint32_t base = regions[region] + 0x1000;
        start(gba, base + low, false);
        cupid_gba_step(gba); CHECK((uint32_t)gba->cpu.gprs[0] == 0x33441122);
        cupid_gba_step(gba); CHECK((uint32_t)gba->cpu.gprs[2] == base + low + 12);
        cupid_gba_step(gba); CHECK((uint32_t)gba->cpu.gprs[3] == base + low + 16);
        cupid_gba_step(gba);
        uint32_t stored; memcpy(&stored, gba->memory.ewram + 0x2004, 4); CHECK(stored == base + low + 16);
        destroy(gba);
    }
    /* Exercise ordinary ARM PC writes, too: MOV pc,r0; LDR pc,[r1]; BX r0. */
    const uint32_t branches[] = {0xE1A0F000, 0xE591F000, 0xE12FFF10};
    for (unsigned branch = 0; branch < 3; ++branch) {
        struct CupidGBA* gba = machine(0, false);
        memcpy(gba->memory.iwram, &branches[branch], 4);
        uint32_t target = GBA_BASE_IWRAM + 0x1002, add = 0xE28F2000;
        memcpy(gba->memory.iwram + 0x1000, &add, 4); memcpy(gba->memory.ewram, &target, 4);
        gba->cpu.gprs[0] = target; gba->cpu.gprs[1] = GBA_BASE_EWRAM;
        start(gba, GBA_BASE_IWRAM, false); cupid_gba_step(gba); cupid_gba_step(gba);
        CHECK((uint32_t)gba->cpu.gprs[2] == target + 8 && gba->cpu.executionMode == MODE_ARM);
        destroy(gba);
    }
}

static void interworking_image(struct CupidGBA* gba, unsigned region, bool bxpc) {
    /* Both Thumb entries select logical +1006, physical ARM word +1004.
     * ADD r2,pc,0; MOV r3,pc; LDR r4,[r1]; STR r4,[r5]; BX r6. */
    const uint32_t words[] = {0xE28F2000, 0xE1A0300F, 0xE5914000, 0xE5854000, 0xE12FFF16, 0xEAFFFFFE};
    uint8_t* bytes = code(gba, region);
    memcpy(bytes + 4, words, sizeof(words));
    uint16_t entry = bxpc ? 0x4778 : 0x4700, thumb[] = {0x2709, 0xE7FE};
    memcpy(bytes + (bxpc ? 2 : 0), &entry, 2); memcpy(bytes + 0x20, thumb, sizeof(thumb));
    uint32_t data = 0x11223344; memcpy(gba->memory.ewram + 0x2000, &data, 4);
    gba->cpu.gprs[0] = regions[region] + 0x1006; gba->cpu.gprs[1] = GBA_BASE_EWRAM + 0x2002;
    gba->cpu.gprs[5] = GBA_BASE_EWRAM + 0x2004; gba->cpu.gprs[6] = regions[region] + 0x1021;
    start(gba, regions[region] + 0x1000 + (bxpc ? 2u : 0u), true);
}
static void check_interworking_result(const struct CupidGBA* gba, unsigned region) {
    CHECK((uint32_t)gba->cpu.gprs[2] == regions[region] + 0x100E);
    CHECK((uint32_t)gba->cpu.gprs[3] == regions[region] + 0x1012);
    CHECK((uint32_t)gba->cpu.gprs[4] == 0x33441122 && gba->cpu.gprs[7] == 9);
    uint32_t stored; memcpy(&stored, gba->memory.ewram + 0x2004, 4); CHECK(stored == 0x33441122);
    CHECK(gba->cpu.executionMode == MODE_THUMB && cupid_arm_instruction_address(&gba->cpu) == regions[region] + 0x1022);
}
static void thumb_bx_and_bx_pc_retain_a1_until_return(void) {
    for (unsigned region = 0; region < 3; ++region) for (unsigned enabled = 0; enabled < 2; ++enabled)
    for (unsigned bxpc = 0; bxpc < 2; ++bxpc) for (unsigned origin = 0; origin < 4; ++origin) {
        struct CupidGBA* gba = machine(clocks[origin], enabled != 0);
        interworking_image(gba, region, bxpc != 0);
        uint64_t before = cupid_timing_global_time(&gba->timing);
        for (unsigned step = 0; step < 7; ++step) cupid_gba_step(gba);
        check_interworking_result(gba, region);
        CHECK(cupid_timing_global_time(&gba->timing) > before);
        destroy(gba);
    }
}

struct Trace { unsigned count; uint32_t address[7], logical[7], opcode[7]; uint64_t clock[7]; bool thumb[7]; };
static void observe(const struct CupidARM* cpu, void* context) {
    struct Trace* trace = context; unsigned i = trace->count++; CHECK(i < 7);
    trace->address[i] = cupid_arm_instruction_address(cpu); trace->logical[i] = (uint32_t)cpu->gprs[ARM_PC];
    trace->opcode[i] = cpu->prefetch[0]; trace->clock[i] = cupid_timing_global_time(&cpu->master->timing);
    trace->thumb[i] = cpu->executionMode == MODE_THUMB;
}
static void physical_issue_observation_is_passive(void) {
    const uint32_t offsets[] = {0, 4, 8, 12, 16, 20, 32};
    const uint32_t opcodes[] = {0x4700, 0xE28F2000, 0xE1A0300F, 0xE5914000, 0xE5854000, 0xE12FFF16, 0x2709};
    for (unsigned region = 0; region < 3; ++region) for (unsigned enabled = 0; enabled < 2; ++enabled)
    for (unsigned bxpc = 0; bxpc < 2; ++bxpc) for (unsigned origin = 0; origin < 4; ++origin) {
        struct CupidGBA* plain = machine(clocks[origin], enabled != 0), *traced = machine(clocks[origin], enabled != 0);
        interworking_image(plain, region, bxpc != 0); interworking_image(traced, region, bxpc != 0);
        struct Trace trace = {0}; traced->cpu.instructionObserver = observe; traced->cpu.instructionObserverContext = &trace;
        for (unsigned step = 0; step < 7; ++step) {
            cupid_gba_step(plain); cupid_gba_step(traced); CHECK(trace.count == step + 1);
            uint32_t address = regions[region] + 0x1000 + offsets[step] + (!step && bxpc ? 2u : 0u);
            CHECK(trace.address[step] == address && trace.opcode[step] == (!step && bxpc ? 0x4778 : opcodes[step]));
            bool thumb = !step || step == 6; CHECK(trace.thumb[step] == thumb);
            CHECK(trace.logical[step] == address + (thumb ? 2u : 6u));
            if (step) CHECK(trace.clock[step] > trace.clock[step - 1]);
            CHECK(!memcmp(plain->cpu.gprs, traced->cpu.gprs, sizeof(plain->cpu.gprs)));
            CHECK(!memcmp(plain->cpu.prefetch, traced->cpu.prefetch, sizeof(plain->cpu.prefetch)));
            CHECK(plain->cpu.cpsr.packed == traced->cpu.cpsr.packed && plain->cpu.spsr.packed == traced->cpu.spsr.packed);
            CHECK(!memcmp(plain->memory.ewram, traced->memory.ewram, GBA_SIZE_EWRAM));
            CHECK(!memcmp(plain->memory.iwram, traced->memory.iwram, GBA_SIZE_IWRAM));
            CHECK(cupid_timing_global_time(&plain->timing) == cupid_timing_global_time(&traced->timing));
        }
        check_interworking_result(traced, region); destroy(plain); destroy(traced);
    }
}
static struct CupidGBA* page_machine(uint64_t clock, uint16_t control) {
    struct CupidGBA* gba = machine(clock, false);
    free(gba->memory.rom);
    gba->memory.rom = calloc(1, 0x40000); CHECK(gba->memory.rom);
    gba->memory.romSize = gba->memory.romImageSize = 0x40000;
    gba->memory.romMask = 0x3FFFF;
    cupid_memory_update_waitstates(gba, control);
    return gba;
}
static unsigned page_fetch(struct CupidGBA* gba, uint32_t address, bool sequential) {
    uint64_t before = cupid_timing_global_time(&gba->timing);
    cupid_bus_fetch(&gba->cpu, address, 4, sequential);
    cupid_bus_finish(&gba->cpu);
    return (unsigned)(cupid_timing_global_time(&gba->timing) - before);
}
static void page_restart_uses_physical_opcode_address(void) {
    static const uint16_t controls[] = {0, 0x14, 0x198, 0x7FF};
    static const unsigned n_table[] = {5, 4, 3, 9}, s_table[] = {3, 5, 9};
    /* The first ADD observes logical A1. The next two MOV words establish
     * that a timing restart neither rotates nor drops pipeline data. */
    static const uint32_t words[] = {0xE28F0000, 0xE3A01031, 0xE3A02042, 0xEAFFFFFE};
    unsigned checks = 0;
    for (unsigned bank = 0; bank < 3; ++bank) for (unsigned setting = 0; setting < 4; ++setting)
    for (unsigned epoch = 0; epoch < 4; ++epoch) for (unsigned low = 0; low <= 2; low += 2) {
        uint16_t control = controls[setting];
        unsigned n = n_table[(control >> (2 + 3 * bank)) & 3];
        unsigned s = control & (1u << (4 + 3 * bank)) ? 2 : s_table[bank];
        uint32_t base = GBA_BASE_ROM0 + bank * 0x02000000u;
        struct CupidGBA* gba = page_machine(clocks[epoch], control);
        memcpy(gba->memory.rom + 0x1FFF8, words, sizeof(words));
        start(gba, base + 0x1FFF8 + low, false);
        uint64_t before = cupid_timing_global_time(&gba->timing);
        cupid_gba_step(gba);
        CHECK(cupid_timing_global_time(&gba->timing) - before == n + s); ++checks;
        CHECK((uint32_t)gba->cpu.gprs[0] == base + 0x20000 + low);
        CHECK(gba->cpu.prefetch[0] == words[1] && gba->cpu.prefetch[1] == words[2]);
        CHECK(cupid_arm_instruction_address(&gba->cpu) == base + 0x1FFFC);
        before = cupid_timing_global_time(&gba->timing);
        cupid_gba_step(gba);
        CHECK(cupid_timing_global_time(&gba->timing) - before == 2 * s); ++checks;
        CHECK((uint32_t)gba->cpu.gprs[1] == 0x31);
        CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == base + 0x20004 + low);
        destroy(gba);

        /* Execute MOV pc,r3 from IWRAM. Its one-clock fetch precedes the
         * refill, whose second word is the first word of the new page. */
        gba = page_machine(clocks[epoch], control);
        memcpy(gba->memory.rom + 0x1FFF8, words, sizeof(words));
        const uint32_t branch = 0xE1A0F003;
        memcpy(gba->memory.iwram + 0x3000, &branch, sizeof(branch));
        gba->cpu.gprs[3] = base + 0x1FFFC + low;
        start(gba, GBA_BASE_IWRAM + 0x3000, false);
        before = cupid_timing_global_time(&gba->timing);
        cupid_gba_step(gba);
        CHECK(cupid_timing_global_time(&gba->timing) - before == 1 + 2 * (n + s)); ++checks;
        CHECK(gba->cpu.prefetch[0] == words[1] && gba->cpu.prefetch[1] == words[2]);
        CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == base + 0x20000 + low);
        CHECK(cupid_arm_instruction_address(&gba->cpu) == base + 0x1FFFC);
        destroy(gba);

        /* A direct request pair independently checks the bus decoder, with
         * prefetch disabled and the preceding word in the old 128 KiB page. */
        gba = page_machine(clocks[epoch], control);
        gba->cpu.gprs[ARM_PC] = base + 0x1FFFC + low;
        cupid_memory_set_active_region(&gba->cpu, base + 0x1FFFC + low);
        CHECK(page_fetch(gba, base + 0x1FFFC + low, false) == n + s);
        CHECK(page_fetch(gba, base + 0x20000 + low, true) == n + s); ++checks;
        destroy(gba);
    }
    CHECK(checks == 384);
    printf("page clocks=%u programs=96 refills=96 request-pairs=96\n", checks);
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "refill")) RUN(refill_keeps_logical_a1_and_unrotated_words);
    else if (!strcmp(argv[1], "operands")) RUN(pc_operands_and_data_rotation_use_distinct_addresses);
    else if (!strcmp(argv[1], "interworking")) RUN(thumb_bx_and_bx_pc_retain_a1_until_return);
    else if (!strcmp(argv[1], "observer")) RUN(physical_issue_observation_is_passive);
    else if (!strcmp(argv[1], "page")) RUN(page_restart_uses_physical_opcode_address);
    else return 2;
    return 0;
}
