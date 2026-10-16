/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "instruction_probe.h"
#include <errno.h>
#include <inttypes.h>

static bool number(const char* text, uint32_t* value) {
    char* end;
    errno = 0;
    unsigned long long parsed = strtoull(text, &end, 0);
    if (errno || !text[0] || text[0] == '-' || *end || parsed > UINT32_MAX) return false;
    *value = (uint32_t)parsed;
    return true;
}

int main(int argc, char** argv) {
    /* ROM BIOS|- CHECKPOINT TERMINAL [ADDRESS OPCODE PRIVILEGE REGISTER VALUE]... */
    if (argc < 10 || (argc - 5) % 5 || (argc - 5) / 5 > 32) return 2;
    unsigned comparisons = (unsigned)(argc - 5) / 5;
    struct CupidIssueExpectation expected[34] = {{0}};
    for (unsigned i = 0; i < comparisons; ++i) {
        uint32_t privilege, reg;
        if (!number(argv[5 + i * 5], &expected[i].address)
            || !number(argv[6 + i * 5], &expected[i].opcode)
            || !number(argv[7 + i * 5], &privilege)
            || !number(argv[8 + i * 5], &reg) || reg > 14
            || !number(argv[9 + i * 5], &expected[i].value)) return 2;
        switch (privilege) {
        case MODE_USR: case MODE_FIQ: case MODE_IRQ: case MODE_SVC:
        case MODE_ABT: case MODE_UND: case MODE_SYS: break;
        default: return 2;
        }
        expected[i].reg = reg;
        expected[i].privilege = (enum CupidPrivMode)privilege;
        expected[i].mode = MODE_ARM;
    }
    for (unsigned i = comparisons; i < comparisons + 2; ++i) {
        if (!number(argv[3 + i - comparisons], &expected[i].address)) return 2;
        expected[i].opcode = i == comparisons ? 0xE92D0003 : 0xEAFFFFFE;
        expected[i].reg = 12;
        expected[i].mode = MODE_ARM;
        expected[i].privilege = MODE_SYS;
    }
    for (unsigned i = 0; i < comparisons + 2; ++i) {
        if (expected[i].address & 3) return 2;
        for (unsigned j = 0; j < i; ++j) if (expected[i].address == expected[j].address) return 2;
    }
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    if (!gba) return 2;
    cupid_gba_create(gba);
    int result = 2;
    if (!cupid_load_rom(gba, argv[1]) || (strcmp(argv[2], "-") && !cupid_load_bios(gba, argv[2]))) goto cleanup;
    cupid_gba_reset(gba);
    struct CupidIssueProbe probe = {.expected=expected, .count=comparisons + 2, .output=stdout};
    gba->cpu.instructionObserver = cupid_test_observe_issue;
    gba->cpu.instructionObserverContext = &probe;
    unsigned frames = 0, steps = 0;
    /* Keep the original cartridge's guards. The terminal must issue through
     * the normal system step, including IRQ/HALT arbitration. */
    while (!probe.terminal && frames < 360 && steps < 150000000
           && cupid_timing_global_time(&gba->timing) < UINT64_C(660) * 280896) {
        cupid_gba_step(gba);
        ++steps;
        if (gba->frameReady) { ++frames; gba->frameReady = false; }
    }
    result = cupid_test_issue_probe_passed(&probe) ? 0 : 1;
    printf("issues=%u/%u checkpoint=%s terminal=%s result=%s\n", probe.seen, probe.count,
        probe.checkpoint ? "yes" : "no", probe.terminal ? "yes" : "no", result ? "FAIL" : "PASS");
    printf("frames=%u cycles=%" PRIu64 " steps=%u\n", frames, cupid_timing_global_time(&gba->timing), steps);
cleanup:
    cupid_gba_destroy(gba);
    free(gba);
    return result;
}
