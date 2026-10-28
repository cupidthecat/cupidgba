/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

enum {
    CORPUS_BASE = 0x08000000, CORPUS_INPUT = 0x08031238,
    CORPUS_LOADED = 0x0803123C, CORPUS_COMPARE = 0x080316A4,
    CORPUS_TERMINAL = 0x080000F0, CORPUS_TABLE = 0x4D0,
    CORPUS_COUNT = 10000, CORPUS_BYTES = 203708
};

struct CorpusRun {
    struct CupidGBA* gba;
    unsigned entries, loads, comparisons, branches, phase;
    uint64_t frames, steps;
    bool failed, terminal, quiet;
};

static uint32_t corpus_word(const uint8_t* data) {
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
           (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

static void corpus_failure(struct CorpusRun* run, const struct CupidARM* cpu,
                           const char* reason, uint32_t actual, uint32_t expected) {
    if (!run->failed && !run->quiet)
        printf("FAIL case=%u pc=%08" PRIx32 " reason=%s actual=%08" PRIx32
               " expected=%08" PRIx32 "\n", run->entries,
               cupid_arm_instruction_address(cpu), reason, actual, expected);
    run->failed = true;
}

/* These sites belong to the immutable input program, including its original
 * result comparisons and conditional failure branches. Observe actual issue;
 * a prefetched instruction or a reported zero result is insufficient. */
static void observe_corpus(const struct CupidARM* cpu, void* context) {
    struct CorpusRun* run = context;
    uint32_t pc = cupid_arm_instruction_address(cpu);
    if (pc != CORPUS_INPUT && pc != CORPUS_LOADED && pc != CORPUS_TERMINAL &&
        (pc < CORPUS_COMPARE || pc > CORPUS_COMPARE + 20 || (pc - CORPUS_COMPARE) % 4)) return;
    if (cpu->executionMode != MODE_ARM) {
        corpus_failure(run, cpu, "mode", cpu->executionMode, MODE_ARM);
        return;
    }
    uint32_t opcode = cpu->prefetch[0];
    if (pc == CORPUS_INPUT) {
        if (opcode != 0xE8BB00E3 || run->phase || run->entries >= CORPUS_COUNT) {
            corpus_failure(run, cpu, "input-order", opcode, 0xE8BB00E3);
            return;
        }
        uint32_t address = CORPUS_BASE + CORPUS_TABLE + 20 * run->entries;
        if ((uint32_t)cpu->gprs[11] != address)
            corpus_failure(run, cpu, "table-address", (uint32_t)cpu->gprs[11], address);
        if ((uint32_t)cpu->gprs[12] != CORPUS_COUNT - run->entries)
            corpus_failure(run, cpu, "remaining", (uint32_t)cpu->gprs[12], CORPUS_COUNT - run->entries);
        ++run->entries;
        run->phase = 1;
        return;
    }
    if (pc == CORPUS_LOADED) {
        if (opcode != 0xE3A0A0FF || run->phase != 1 || !run->entries) {
            corpus_failure(run, cpu, "loaded-order", opcode, 0xE3A0A0FF);
            return;
        }
        const unsigned registers[] = {0, 1, 5, 6, 7};
        const uint8_t* data = run->gba->memory.rom + CORPUS_TABLE + 20 * (run->entries - 1);
        for (unsigned i = 0; i < 5; ++i) {
            uint32_t expected = corpus_word(data + 4 * i);
            if ((uint32_t)cpu->gprs[registers[i]] != expected)
                corpus_failure(run, cpu, "loaded-record", (uint32_t)cpu->gprs[registers[i]], expected);
        }
        ++run->loads;
        run->phase = 2;
        return;
    }
    if (pc == CORPUS_TERMINAL) {
        run->terminal = true;
        if (opcode != 0xEAFFFFFE || run->phase || run->entries != CORPUS_COUNT ||
            run->loads != CORPUS_COUNT || run->comparisons != 3 * CORPUS_COUNT ||
            run->branches != 3 * CORPUS_COUNT)
            corpus_failure(run, cpu, "terminal-coverage", run->comparisons, 3 * CORPUS_COUNT);
        return;
    }
    unsigned stage = (pc - CORPUS_COMPARE) / 4;
    if (run->phase != stage + 2 || !run->entries) {
        corpus_failure(run, cpu, "comparison-order", run->phase, stage + 2);
        return;
    }
    const uint32_t instructions[] = {
        0xE1530005, 0x1A000004, 0xE1540006, 0x1A000002, 0xE15B0007, 0x1A000000
    };
    if (opcode != instructions[stage])
        corpus_failure(run, cpu, "opcode", opcode, instructions[stage]);
    if (stage & 1) {
        ++run->branches;
        if (!cpu->cpsr.z) corpus_failure(run, cpu, "original-bne-condition", 0, 1);
    } else {
        const unsigned left[] = {3, 4, 11}, right[] = {5, 6, 7};
        unsigned pair = stage / 2;
        const uint8_t* data = run->gba->memory.rom + CORPUS_TABLE + 20 * (run->entries - 1);
        uint32_t expected = pair < 2 ? corpus_word(data + 8 + 4 * pair)
            : (corpus_word(data + 16) & 0x0F000000u) << 4;
        ++run->comparisons;
        /* Check the restored expected register too. Corrupting both operands
         * to the same value must not turn a bad result into a passing CMP. */
        if ((uint32_t)cpu->gprs[right[pair]] != expected)
            corpus_failure(run, cpu, "immutable-expected-word", (uint32_t)cpu->gprs[right[pair]], expected);
        if ((uint32_t)cpu->gprs[left[pair]] != expected)
            corpus_failure(run, cpu, "original-compare", (uint32_t)cpu->gprs[left[pair]], expected);
    }
    run->phase = stage == 5 ? 0 : run->phase + 1;
}

static bool corpus_passed(const struct CorpusRun* run) {
    return run->terminal && !run->failed && !run->phase &&
        run->entries == CORPUS_COUNT && run->loads == CORPUS_COUNT &&
        run->comparisons == 3 * CORPUS_COUNT && run->branches == 3 * CORPUS_COUNT;
}

static bool corpus_boundary(struct CupidGBA* gba, uint64_t steps, void* context) {
    struct CorpusRun* run = context;
    run->steps = steps;
    if (gba->frameReady) {
        ++run->frames;
        gba->frameReady = false;
    }
    return !run->failed && !run->terminal && run->frames < 1500 &&
        cupid_timing_global_time(&gba->timing) < UINT64_C(1500) * VIDEO_TOTAL_LENGTH;
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Usage: %s ROM EWRAM-output [BIOS]\n", argv[0]);
        return 2;
    }
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    if (!gba) return 2;
    cupid_gba_create(gba);
    int result = 2;
    if (!cupid_load_rom(gba, argv[1]) || (argc == 4 && !cupid_load_bios(gba, argv[3]))) goto cleanup;
    /* romSize is mapped capacity. The launcher verifies the complete source
     * file's exact length, digest and layout before starting this process. */
    if (gba->memory.romSize < CORPUS_BYTES ||
        corpus_word(gba->memory.rom + CORPUS_TABLE - 4) != CORPUS_COUNT) goto cleanup;
    cupid_gba_reset(gba);
    struct CorpusRun run = {.gba = gba};
    gba->cpu.instructionObserver = observe_corpus;
    gba->cpu.instructionObserverContext = &run;
    cupid_gba_run_steps(gba, 150000000, corpus_boundary, &run);
    bool passed = corpus_passed(&run);
    printf("cases=%u/%u loads=%u comparisons=%u/%u branches=%u/%u terminal=%s "
           "frames=%" PRIu64 " steps=%" PRIu64 " cycles=%" PRIu64 " result=%s\n",
           run.entries, CORPUS_COUNT, run.loads, run.comparisons, 3 * CORPUS_COUNT,
           run.branches, 3 * CORPUS_COUNT, run.terminal ? "yes" : "no", run.frames,
           run.steps, cupid_timing_global_time(&gba->timing), passed ? "PASS" : "FAIL");
    FILE* file = fopen(argv[2], "wb");
    if (!file) goto cleanup;
    size_t written = fwrite(gba->memory.ewram, 1, GBA_SIZE_EWRAM, file);
    int closed = fclose(file);
    if (written != GBA_SIZE_EWRAM || closed) goto cleanup;
    result = passed ? 0 : 1;
cleanup:
    cupid_gba_destroy(gba);
    free(gba);
    return result;
}
