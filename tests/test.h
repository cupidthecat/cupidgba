/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      test.h
 * @author    cupidthecat
 * @brief     Always-enabled regression assertions and test invocation helpers.
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
#ifndef CUPID_TEST_H
#define CUPID_TEST_H

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

/* Checks remain enabled in release builds. */
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                        \
            exit(EXIT_FAILURE);                                                                    \
        }                                                                                          \
    } while (0)

#define RUN(test)                                                                                  \
    do {                                                                                           \
        test();                                                                                    \
        puts("PASS " #test);                                                                       \
    } while (0)
#endif
