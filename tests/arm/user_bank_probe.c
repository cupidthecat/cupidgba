/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"
#include <errno.h>

struct Guard { uint32_t address, opcode; bool zero; };
struct Observation {
    uint32_t checkpoint, terminal;
    unsigned resultRegister, guardCount;
    struct Guard guards[32];
};
struct Result { bool checkpoint, terminal, valid; unsigned guards; uint64_t fingerprint; };

static uint64_t bytes_hash(uint64_t value, const void* data, size_t size) {
    const uint8_t* bytes = data;
    for (size_t i = 0; i < size; ++i) value = (value ^ bytes[i]) * UINT64_C(1099511628211);
    return value;
}

static struct Result observe(struct CupidGBA* gba, const struct Observation* expected) {
    struct Result result = {.valid = true};
    unsigned frames = 0;
    for (unsigned steps = 0; steps < 150000000 && frames < 360 &&
         cupid_timing_global_time(&gba->timing) < UINT64_C(660) * 280896; ++steps) {
        struct CupidARM* cpu = &gba->cpu;
        bool arm = cpu->executionMode == MODE_ARM;
        uint32_t pc = (uint32_t)cpu->gprs[ARM_PC] - (arm ? 4u : 2u);
        bool boundary = pc == expected->checkpoint || pc == expected->terminal;
        for (unsigned g = 0; g < expected->guardCount; ++g) {
            if (pc != expected->guards[g].address) continue;
            boundary = true;
            result.valid &= arm && g == result.guards && cpu->prefetch[0] == expected->guards[g].opcode
                && cpu->cpsr.z == expected->guards[g].zero;
            ++result.guards;
            break;
        }
        /* These cartridges disable interrupts. Prove arbitration cannot replace
         * a sampled instruction before calling the normal system step. */
        if (boundary) result.valid &= !cpu->halted && !gba->stopped && !gba->irq.line && !gba->ie && !gba->ime;
        if (pc == expected->checkpoint) {
            result.valid &= !result.checkpoint && arm && cpu->privilegeMode == MODE_FIQ
                && cpu->prefetch[0] == (0xE1A00000u | expected->resultRegister)
                && cpu->gprs[expected->resultRegister] == 0 && result.guards == expected->guardCount;
            result.checkpoint = true;
        }
        if (pc == expected->terminal) {
            result.valid &= arm && cpu->prefetch[0] == 0xEAFFFFFE && cpu->gprs[0] == 0;
            cupid_gba_step(gba); /* Require the terminal branch to issue. */
            result.terminal = cpu->executionMode == MODE_ARM
                && (uint32_t)cpu->gprs[ARM_PC] == expected->terminal + 4
                && cpu->prefetch[0] == 0xEAFFFFFE;
            break;
        }
        cupid_gba_step(gba);
        if (gba->frameReady) { ++frames; gba->frameReady = false; }
    }
    result.valid &= result.checkpoint && result.terminal && result.guards == expected->guardCount;
    uint64_t hash = UINT64_C(14695981039346656037), clock = cupid_timing_global_time(&gba->timing);
    hash = bytes_hash(hash, gba->cpu.gprs, sizeof(gba->cpu.gprs));
    hash = bytes_hash(hash, gba->cpu.bankedRegisters, sizeof(gba->cpu.bankedRegisters));
    hash = bytes_hash(hash, &gba->cpu.cpsr, sizeof(gba->cpu.cpsr));
    hash = bytes_hash(hash, gba->memory.iwram, GBA_SIZE_IWRAM);
    hash = bytes_hash(hash, gba->memory.ewram, GBA_SIZE_EWRAM);
    hash = bytes_hash(hash, gba->memory.io, GBA_SIZE_IO);
    result.fingerprint = bytes_hash(hash, &clock, sizeof(clock));
    return result;
}

static bool number(const char* text, uint32_t* value) {
    char* end;
    errno = 0;
    unsigned long long parsed = strtoull(text, &end, 0);
    if (errno || !text[0] || text[0] == '-' || *end || parsed > UINT32_MAX) return false;
    *value = (uint32_t)parsed;
    return true;
}

int cupid_test_user_bank_probe(int argc, char** argv) {
    if (argc < 7 || (argc - 7) % 3 || (argc - 7) / 3 > 32) return 2;
    struct Observation expected = {.guardCount = (unsigned)(argc - 7) / 3};
    uint32_t reg;
    if (!number(argv[4], &expected.checkpoint) || !number(argv[5], &reg) || reg > 14
        || !number(argv[6], &expected.terminal)) return 2;
    expected.resultRegister = reg;
    for (unsigned i = 0; i < expected.guardCount; ++i) {
        uint32_t zero;
        if (!number(argv[7 + i * 3], &expected.guards[i].address)
            || !number(argv[8 + i * 3], &expected.guards[i].opcode)
            || !number(argv[9 + i * 3], &zero) || zero > 1) return 2;
        expected.guards[i].zero = zero != 0;
    }
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    if (!cupid_load_rom(gba, argv[2]) || (strcmp(argv[3], "-") && !cupid_load_bios(gba, argv[3]))) {
        cupid_gba_destroy(gba); free(gba); return 2;
    }
    cupid_gba_reset(gba);
    struct Result result = observe(gba, &expected);
    printf("checkpoint=%s guards=%u/%u terminal=%s r0=%u result=%s\n",
        result.checkpoint ? "yes" : "no", result.guards, expected.guardCount,
        result.terminal ? "yes" : "no", (unsigned)gba->cpu.gprs[0], result.valid ? "PASS" : "FAIL");
    cupid_gba_destroy(gba); free(gba);
    return result.valid ? 0 : 1;
}

static struct Result owned(bool changedInput, unsigned check) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba); cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    cupid_arm_set_priv_mode(&gba->cpu, MODE_FIQ);
    gba->cpu.cpsr.packed = MODE_FIQ | 0xC0;
    gba->cpu.irqDisableLatch = true;
    /* BNE bypasses failure recording. Both inputs reach R0=0 and self-branch;
     * observing the original comparison must still reject the changed input. */
    const uint32_t image[] = {0xE3510003,0x1A000000,0xE1A00000,0xE1A00002,0xE1A00000,0xEAFFFFFE};
    memcpy(gba->memory.iwram + 0x1000, image, sizeof(image));
    gba->cpu.gprs[1] = changedInput ? 2 : 3;
    gba->cpu.gprs[2] = 0;
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x1000;
    CupidARMWritePC(&gba->cpu);
    struct Observation expected = {.checkpoint=GBA_BASE_IWRAM + 0x100C,
        .terminal=GBA_BASE_IWRAM + 0x1014, .resultRegister=2};
    if (check) {
        expected.guardCount = 1;
        expected.guards[0] = (struct Guard){GBA_BASE_IWRAM + 0x1004,0x1A000000,true};
    }
    if (check == 2) expected.guards[0].opcode ^= 1;
    if (check == 3) {
        expected.guardCount = 2;
        expected.guards[1] = (struct Guard){GBA_BASE_IWRAM + 0x1018,0x1A000000,true};
    }
    struct Result result = observe(gba, &expected);
    CHECK(result.checkpoint && result.terminal && gba->cpu.gprs[0] == 0);
    cupid_gba_destroy(gba); free(gba);
    return result;
}

void cupid_test_user_bank_observer(void) {
    for (unsigned changed = 0; changed < 2; ++changed) {
        struct Result legacy = owned(changed != 0, 0);
        CHECK(legacy.valid);
        for (unsigned check = 1; check <= 3; ++check) {
            struct Result result = owned(changed != 0, check);
            CHECK(result.fingerprint == legacy.fingerprint);
            CHECK(result.valid == (!changed && check == 1));
        }
    }
    puts("PASS user-bank observer: complete zero-result terminals, changed-input/opcode/missing controls rejected, state/clocks retained");
}
