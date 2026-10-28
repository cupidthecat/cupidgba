/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

struct Issued { unsigned count; uint32_t address[3], opcode[3]; };
static unsigned opcodeReads;

static bool count_opcode(struct CupidARM* cpu, uint32_t address, unsigned width, uint32_t* value) {
    (void)cpu; (void)address; (void)value;
    CHECK(width == WORD_SIZE_ARM);
    ++opcodeReads;
    return false;
}

static void observe_issue(const struct CupidARM* cpu, void* context) {
    struct Issued* issued = context;
    CHECK(cpu->executionMode == MODE_ARM && issued->count < 3);
    unsigned i = issued->count++;
    issued->address[i] = cupid_arm_instruction_address(cpu);
    issued->opcode[i] = cpu->prefetch[0];
}

static void run(const char* group, uint32_t base) {
    uint32_t middle;
    bool branch = false, control = false, user = false, flags = false;
    if (!strcmp(group, "register")) { middle = 0xE128F003; flags = true; }
    else if (!strcmp(group, "immediate")) { middle = 0xE328F4A0; flags = true; }
    else if (!strcmp(group, "register-control")) { middle = 0xE121F003; control = true; }
    else if (!strcmp(group, "immediate-control")) { middle = 0xE321F093; control = true; }
    else if (!strcmp(group, "user-register")) { middle = 0xE121F003; control = user = true; }
    else if (!strcmp(group, "user-immediate")) { middle = 0xE321F093; control = user = true; }
    else if (!strcmp(group, "ordinary")) middle = 0xE1A03003;
    else { CHECK(!strcmp(group, "branch")); middle = 0xEAFFFFFF; branch = true; }

    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba != NULL);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    if (user) {
        gba->cpu.cpsr.priv = MODE_USR;
        cupid_arm_set_priv_mode(&gba->cpu, MODE_USR);
    }

    const uint32_t original = 0xE3A02001, replacement = 0xE3A02002;
    /* STR changes the instruction after the middle operation. That opcode
     * has already been fetched before the store completes. A branch refills
     * it from memory; an ordinary ARM-state PSR transfer keeps it queued. */
    const uint32_t code[] = {0xE5810000, middle, original, 0xEAFFFFFE, 0xE1A00000};
    uint8_t* ram = base == GBA_BASE_EWRAM ? gba->memory.ewram : gba->memory.iwram;
    memcpy(ram + 0x1000, code, sizeof(code));
    uint32_t start = base + 0x1000;
    gba->cpu.gprs[0] = (int32_t)replacement;
    gba->cpu.gprs[1] = (int32_t)(start + 8);
    gba->cpu.gprs[3] = (int32_t)0xA0000093;
    _CupidSetMode(&gba->cpu, MODE_ARM);
    gba->cpu.memory.timed = true;
    gba->cpu.memory.readOpcode = count_opcode;
    opcodeReads = 0;
    gba->cpu.gprs[ARM_PC] = (int32_t)start;
    CupidARMWritePC(&gba->cpu);
    struct Issued issued = {0};
    gba->cpu.instructionObserver = observe_issue;
    gba->cpu.instructionObserverContext = &issued;
    for (unsigned i = 0; i < 3; i++) cupid_gba_step(gba);

    uint32_t stored;
    memcpy(&stored, ram + 0x1008, sizeof(stored));
    CHECK(stored == replacement && issued.count == 3);
    for (unsigned i = 0; i < 3; i++) CHECK(issued.address[i] == start + i * 4);
    CHECK(issued.opcode[0] == code[0] && issued.opcode[1] == middle);
    uint32_t expectedOpcode = branch ? replacement : original;
    if (issued.opcode[2] != expectedOpcode)
        fprintf(stderr, "%s region=%02x issued=%08x expected=%08x r2=%u opcode_reads=%u\n",
            group, base >> 24, issued.opcode[2], expectedOpcode, (unsigned)gba->cpu.gprs[2], opcodeReads);
    CHECK(issued.opcode[2] == expectedOpcode);
    CHECK(gba->cpu.gprs[2] == (branch ? 2 : 1));
    CHECK(opcodeReads == (branch ? 7u : 5u));
    CHECK((gba->cpu.cpsr.packed & 0xF0000000u) == (flags ? 0xA0000000u : 0));
    CHECK(gba->cpu.privilegeMode == (user ? MODE_USR : control ? MODE_SVC : MODE_SYS));
    CHECK(gba->cpu.cpsr.i && gba->cpu.executionMode == MODE_ARM);
    cupid_gba_destroy(gba);
    free(gba);
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    run(argv[1], GBA_BASE_EWRAM);
    run(argv[1], GBA_BASE_IWRAM);
    printf("PASS %s (EWRAM and IWRAM)\n", argv[1]);
    return 0;
}
