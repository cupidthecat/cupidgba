/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"
#include <errno.h>

struct IRQGuard { uint32_t address, opcode; bool thumb, zero; };
struct IRQExpectation {
    uint32_t checkpoint, terminal;
    unsigned count;
    struct IRQGuard guards[32];
};
struct IRQObservation {
    const struct IRQExpectation* expected;
    unsigned next;
    bool valid, checkpoint, terminal;
    uint64_t fingerprint;
};
static void observe_irq_instruction(const struct CupidARM* cpu, void* context) {
    struct IRQObservation* observation = context;
    const struct IRQExpectation* expected = observation->expected;
    const bool thumb = cpu->executionMode == MODE_THUMB;
    uint32_t address = (uint32_t)cpu->gprs[ARM_PC] - (thumb ? 2u : 4u);
    for (unsigned i = 0; i < expected->count; ++i) {
        const struct IRQGuard* guard = &expected->guards[i];
        if (address != guard->address) continue;
        bool valid = observation->next == i && thumb == guard->thumb
            && cpu->prefetch[0] == guard->opcode && cpu->cpsr.z == guard->zero;
        if (!valid) printf("comparison=%u pc=%08x opcode=%08x expected=%08x thumb=%u z=%u expected-z=%u r0=%08x r8=%08x\n",
            i, address, cpu->prefetch[0], guard->opcode, thumb, cpu->cpsr.z, guard->zero,
            (unsigned)cpu->gprs[0], (unsigned)cpu->gprs[8]);
        observation->valid &= valid;
        ++observation->next;
        break;
    }
    if (address == expected->checkpoint) {
        observation->valid &= !observation->checkpoint && !thumb && !cpu->gprs[12]
            && cpu->prefetch[0] == 0xE92D0003 && observation->next == expected->count;
        observation->checkpoint = true;
    }
    if (address == expected->terminal) {
        observation->valid &= !thumb && !cpu->gprs[12] && cpu->prefetch[0] == 0xEAFFFFFE;
        observation->terminal = true;
    }
}
static uint64_t hash_bytes(uint64_t value, const void* data, size_t size) {
    const uint8_t* bytes = data;
    for (size_t i = 0; i < size; ++i) value = (value ^ bytes[i]) * UINT64_C(1099511628211);
    return value;
}
static struct IRQObservation execute(struct CupidGBA* gba, const struct IRQExpectation* expected, unsigned budget) {
    struct IRQObservation observation = {.expected = expected, .valid = true};
    gba->cpu.instructionObserver = observe_irq_instruction;
    gba->cpu.instructionObserverContext = &observation;
    unsigned frames = 0;
    for (unsigned steps = 0; steps < budget && frames < 360 &&
         cupid_timing_global_time(&gba->timing) < UINT64_C(660) * 280896; ++steps) {
        cupid_gba_step(gba);
        if (observation.terminal) {
            /* The callback sees issue after IRQ/HALT arbitration. Require the
             * original self-branch to complete, rather than just peek its PC. */
            observation.valid &= gba->cpu.executionMode == MODE_ARM
                && (uint32_t)gba->cpu.gprs[ARM_PC] == expected->terminal + 4
                && gba->cpu.prefetch[0] == 0xEAFFFFFE;
            break;
        }
        if (gba->frameReady) { ++frames; gba->frameReady = false; }
    }
    gba->cpu.instructionObserver = NULL; gba->cpu.instructionObserverContext = NULL;
    observation.valid &= observation.checkpoint && observation.terminal && observation.next == expected->count;
    uint64_t hash = UINT64_C(14695981039346656037), clock = cupid_timing_global_time(&gba->timing);
    hash = hash_bytes(hash, gba->cpu.gprs, sizeof(gba->cpu.gprs));
    hash = hash_bytes(hash, gba->cpu.bankedRegisters, sizeof(gba->cpu.bankedRegisters));
    hash = hash_bytes(hash, &gba->cpu.cpsr, sizeof(gba->cpu.cpsr));
    hash = hash_bytes(hash, gba->cpu.prefetch, sizeof(gba->cpu.prefetch));
    hash = hash_bytes(hash, gba->memory.ewram, GBA_SIZE_EWRAM);
    hash = hash_bytes(hash, gba->memory.iwram, GBA_SIZE_IWRAM);
    hash = hash_bytes(hash, gba->memory.io, GBA_SIZE_IO);
    observation.fingerprint = hash_bytes(hash, &clock, sizeof(clock));
    observation.expected = NULL;
    return observation;
}
static bool number(const char* text, uint32_t* value) {
    char* end; errno = 0;
    unsigned long long parsed = strtoull(text, &end, 0);
    if (errno || !text[0] || text[0] == '-' || *end || parsed > UINT32_MAX) return false;
    *value = (uint32_t)parsed; return true;
}
int cupid_test_irq_source_probe(int argc, char** argv) {
    if (argc < 10 || (argc - 6) % 4 || (argc - 6) / 4 > 32) return 2;
    struct IRQExpectation expected = {.count = (unsigned)(argc - 6) / 4};
    if (!number(argv[4], &expected.checkpoint) || !number(argv[5], &expected.terminal)) return 2;
    for (unsigned i = 0; i < expected.count; ++i) {
        uint32_t thumb, zero; struct IRQGuard* guard = &expected.guards[i];
        if (!number(argv[6+i*4], &guard->address) || !number(argv[7+i*4], &thumb) || thumb > 1
            || !number(argv[8+i*4], &guard->opcode) || !number(argv[9+i*4], &zero) || zero > 1
            || (thumb && guard->opcode > UINT16_MAX)) return 2;
        guard->thumb = thumb != 0; guard->zero = zero != 0;
    }
    struct CupidGBA* gba = calloc(1, sizeof(*gba)); CHECK(gba);
    cupid_gba_create(gba);
    if (!cupid_load_rom(gba, argv[2]) || !cupid_load_bios(gba, argv[3])) { cupid_gba_destroy(gba); free(gba); return 2; }
    cupid_gba_reset(gba);
    struct IRQObservation observation = execute(gba, &expected, 150000000);
    printf("checkpoint=%s comparisons=%u/%u terminal=%s r12=%u result=%s\n",
        observation.checkpoint ? "yes" : "no", observation.next, expected.count,
        observation.terminal ? "yes" : "no", (unsigned)gba->cpu.gprs[12], observation.valid ? "PASS" : "FAIL");
    cupid_gba_destroy(gba); free(gba); return observation.valid ? 0 : 1;
}
static struct IRQObservation owned(bool thumb, bool changed, unsigned check) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba)); CHECK(gba);
    cupid_gba_create(gba); cupid_gba_reset(gba); cupid_timing_clear(&gba->timing);
    const uint32_t arm[] = {0xE3500003,0x1A000000,0xE1A00000,0xE1A00000,0xE92D0003,0xE1A00000,0xEAFFFFFE};
    const uint16_t narrow[] = {0x2803,0xD100,0x46C0,0x46C0,0x4708};
    const uint32_t tail[] = {0xE92D0003,0xE1A00000,0xEAFFFFFE};
    memcpy(gba->memory.iwram + 0x1000, thumb ? (const void*)narrow : (const void*)arm, thumb ? sizeof(narrow) : sizeof(arm));
    memcpy(gba->memory.iwram + 0x1100, tail, sizeof(tail));
    gba->cpu.gprs[0] = changed ? 2 : 3; gba->cpu.gprs[1] = GBA_BASE_IWRAM + 0x1100;
    gba->cpu.gprs[12] = 0; gba->cpu.gprs[ARM_SP] = GBA_BASE_IWRAM + 0x7000;
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x1000;
    if (thumb) CupidThumbWritePC(&gba->cpu); else CupidARMWritePC(&gba->cpu);
    struct IRQExpectation expected = {.checkpoint = GBA_BASE_IWRAM + (thumb ? 0x1100 : 0x1010),
        .terminal = GBA_BASE_IWRAM + (thumb ? 0x1108 : 0x1018)};
    if (check) {
        expected.count = 1;
        expected.guards[0] = (struct IRQGuard){GBA_BASE_IWRAM + (thumb ? 0x1002 : 0x1004), thumb ? 0xD100u : 0x1A000000u,thumb,true};
    }
    if (check == 2) expected.guards[0].opcode ^= 1;
    if (check == 3) {
        expected.count = 2;
        expected.guards[1] = (struct IRQGuard){GBA_BASE_IWRAM + 0x1200,0x1A000000,false,true};
    }
    if (check == 4) expected.guards[0].thumb = !expected.guards[0].thumb;
    struct IRQObservation result = execute(gba, &expected, 10000);
    CHECK(result.checkpoint && result.terminal && !gba->cpu.gprs[12]);
    cupid_gba_destroy(gba); free(gba); return result;
}
void cupid_test_irq_source_observer(void) {
    for (unsigned thumb = 0; thumb < 2; ++thumb) for (unsigned changed = 0; changed < 2; ++changed) {
        struct IRQObservation baseline = owned(thumb != 0, changed != 0, 0); CHECK(baseline.valid);
        for (unsigned check = 1; check <= 4; ++check) {
            struct IRQObservation result = owned(thumb != 0, changed != 0, check);
            CHECK(result.fingerprint == baseline.fingerprint);
            CHECK(result.valid == (!changed && check == 1));
        }
    }
    puts("PASS issued IRQ comparison observations: bypassed failure/opcode/missing controls rejected; state and clocks retained");
}
