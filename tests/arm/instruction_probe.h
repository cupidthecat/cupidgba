/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      instruction_probe.h
 * @author    cupidthecat
 * @brief     Issued-instruction operand observation and probe acceptance.
 * @date      2026-11-16
 *
 * @copyright Copyright (c) 2026 cupidthecat
 *
 * This file is part of CupidGBA.
 *
 * CupidGBA is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CupidGBA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CupidGBA.  If not, see <https://gnu.org>.
 */
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
    const struct CupidIssueExpectation *expected;
    unsigned count, seen;
    bool failed, checkpoint, terminal;
    FILE *output;
};

void cupid_test_observe_issue(const struct CupidARM *cpu, void *context);
bool cupid_test_issue_probe_passed(const struct CupidIssueProbe *probe);

#endif
