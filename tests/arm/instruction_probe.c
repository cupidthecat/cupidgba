/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "instruction_probe.h"
#include <inttypes.h>

void cupid_test_observe_issue(const struct CupidARM* cpu, void* context) {
    struct CupidIssueProbe* probe = context;
    uint32_t address = cupid_arm_instruction_address(cpu);
    for (unsigned i = 0; i < probe->count; ++i) {
        const struct CupidIssueExpectation* expected = &probe->expected[i];
        if (address != expected->address) continue;
        uint32_t value = (uint32_t)cpu->gprs[expected->reg];
        bool match = probe->seen == i && cpu->executionMode == expected->mode
            && cpu->privilegeMode == expected->privilege
            && cpu->prefetch[0] == expected->opcode && value == expected->value;
        probe->failed |= !match;
        ++probe->seen;
        if (i + 2 == probe->count) probe->checkpoint = true;
        if (i + 1 == probe->count) probe->terminal = true;
        if (probe->output) {
            fprintf(probe->output, "issue=%u pc=%08" PRIx32 " mode=%s privilege=%02x opcode=%08" PRIx32
                " r%u=%08" PRIx32 " expected=%08" PRIx32 " result=%s\n", i, address,
                cpu->executionMode == MODE_ARM ? "arm" : "thumb", (unsigned)cpu->privilegeMode,
                cpu->prefetch[0], expected->reg, value, expected->value, match ? "PASS" : "FAIL");
        }
        break;
    }
}

bool cupid_test_issue_probe_passed(const struct CupidIssueProbe* probe) {
    return probe->count >= 3 && !probe->failed && probe->seen == probe->count
        && probe->checkpoint && probe->terminal;
}
