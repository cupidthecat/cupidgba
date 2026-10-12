/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_TEST_H
#define CUPID_TEST_H

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

/* Checks remain enabled in release builds. */
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

#define RUN(test) do { test(); puts("PASS " #test); } while (0)
#endif
