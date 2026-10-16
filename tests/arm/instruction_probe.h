/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_TEST_INSTRUCTION_PROBE_H
#define CUPID_TEST_INSTRUCTION_PROBE_H

#include "cupid/arm.h"
#include <stdio.h>

struct CupidIssueExpectation {
    uint32_t address, opcode, value;
    unsigned reg;
    enum CupidExecMode mode;
    enum CupidPrivMode privilege;
};

/* The last two expectations are the result checkpoint and terminal. Records
 * belong to the host; the callback never writes through the CPU pointer. */
struct CupidIssueProbe {
    const struct CupidIssueExpectation* expected;
    unsigned count, seen;
    bool failed, checkpoint, terminal;
    FILE* output;
};

void cupid_test_observe_issue(const struct CupidARM* cpu, void* context);
bool cupid_test_issue_probe_passed(const struct CupidIssueProbe* probe);

#endif
