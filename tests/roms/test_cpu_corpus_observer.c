/* SPDX-License-Identifier: GPL-3.0-or-later */
#define main corpus_runner_main
#include "cpu_corpus_runner.c"
#undef main
#include "../test.h"

static void put(uint8_t* data, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data[i] = (uint8_t)(value >> (8 * i));
}

static void issue(struct CupidARM* cpu, struct CorpusRun* run, uint32_t pc, uint32_t opcode) {
    /* Match the issue boundary's R15 port, not an ALU operand's PC value. */
    cpu->gprs[ARM_PC] = (int32_t)(pc + (cpu->executionMode == MODE_ARM ? 4 : 2));
    cpu->prefetch[0] = opcode;
    uint8_t before[sizeof(*cpu)];
    memcpy(before, cpu, sizeof(*cpu));
    observe_corpus(cpu, run);
    CHECK(memcmp(before, cpu, sizeof(*cpu)) == 0);
}

/* Synthetic issue transcripts test the host protocol and read-only contract.
 * The immutable cartridge groups supply actual CPU execution separately. */
static void transcript(unsigned fault) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    gba->memory.rom = calloc(1, CORPUS_BYTES);
    CHECK(gba->memory.rom);
    struct CupidARM* cpu = &gba->cpu;
    cpu->executionMode = MODE_ARM;
    cpu->privilegeMode = MODE_SYS;
    struct CorpusRun run = {.gba = gba, .quiet = true};
    const uint32_t instructions[] = {
        0xE1530005, 0x1A000004, 0xE1540006, 0x1A000002, 0xE15B0007, 0x1A000000
    };
    if (fault == 15) issue(cpu, &run, CORPUS_TERMINAL, 0xEAFFFFFE);
    for (unsigned index = 0; index < CORPUS_COUNT && !run.failed; ++index) {
        uint32_t values[] = {index + 7, ~index, index * 3, index ^ 0x12345678u,
                             (index & 15) << 24};
        uint8_t* record = gba->memory.rom + CORPUS_TABLE + 20 * index;
        for (unsigned i = 0; i < 5; ++i) put(record + 4 * i, values[i]);
        cpu->gprs[11] = CORPUS_BASE + CORPUS_TABLE + 20 * index;
        cpu->gprs[12] = CORPUS_COUNT - index;
        bool first = index == 0;
        if (first && fault == 1) cpu->executionMode = MODE_THUMB;
        if (first && fault == 2) cpu->gprs[11] += 20;
        if (first && fault == 3) cpu->gprs[12]--;
        if (!(first && fault == 4))
            issue(cpu, &run, CORPUS_INPUT, first && fault == 5 ? 0 : 0xE8BB00E3);
        const unsigned registers[] = {0, 1, 5, 6, 7};
        for (unsigned i = 0; i < 5; ++i) cpu->gprs[registers[i]] = (int32_t)values[i];
        if (first && fault == 6) cpu->gprs[0] ^= 1;
        if (!run.failed) issue(cpu, &run, CORPUS_LOADED, 0xE3A0A0FF);
        cpu->gprs[3] = (int32_t)values[2];
        cpu->gprs[4] = (int32_t)values[3];
        cpu->gprs[11] = cpu->gprs[7] = (int32_t)((values[4] & 0x0F000000u) << 4);
        cpu->cpsr.z = 1;
        if (first && fault == 7) cpu->gprs[3] ^= 1;
        if (first && fault == 8) cpu->gprs[3] = ++cpu->gprs[5];
        for (unsigned stage = 0; stage < 6 && !run.failed; ++stage) {
            if (first && ((fault == 9 && stage == 0) || (fault == 10 && stage == 1))) continue;
            if (first && fault == 11 && stage == 1) cpu->cpsr.z = 0;
            issue(cpu, &run, CORPUS_COMPARE + 4 * stage,
                  first && fault == 12 && stage == 0 ? 0 : instructions[stage]);
            if (first && fault == 13 && stage == 0 && !run.failed)
                issue(cpu, &run, CORPUS_COMPARE, instructions[0]);
        }
    }
    if (!run.failed && fault != 14)
        issue(cpu, &run, CORPUS_TERMINAL, fault == 16 ? 0 : 0xEAFFFFFE);
    if (corpus_passed(&run) != (fault == 0))
        fprintf(stderr, "fault=%u failed=%u terminal=%u phase=%u entries=%u loads=%u comparisons=%u branches=%u\n",
                fault, (unsigned)run.failed, (unsigned)run.terminal, run.phase,
                run.entries, run.loads, run.comparisons, run.branches);
    CHECK(corpus_passed(&run) == (fault == 0));
    CHECK(run.failed || fault == 0 || fault == 14);
    free(gba->memory.rom);
    free(gba);
}

int main(void) {
    for (unsigned fault = 0; fault <= 16; ++fault) transcript(fault);
    printf("PASS CPU corpus observer: complete protocol and 16 invalid transcripts; CPU unchanged\n");
    return 0;
}
