/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_runner_arguments.c
 * @brief Strict headless-runner numeric argument parsing.
 */
#define main cupid_argument_runner_main
#include "../rom_runner.c"
#undef main
#include "../test.h"

static void numeric_arguments(void) {
    const char *invalid[] = {NULL,
                             "",
                             "-1",
                             " -1",
                             "\t-1",
                             "+1",
                             " 1",
                             "1 ",
                             "1\n",
                             "0x",
                             "0xG",
                             "09",
                             "1x",
                             "18446744073709551616",
                             "0x10000000000000000"};
    uint64_t value = 73;
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        CHECK(!number(invalid[i], &value));
        CHECK(value == 73);
    }
    for (unsigned character = 1; character < 128; ++character) {
        if (character >= '0' && character <= '9')
            continue;
        char text[] = {(char)character, '1', 0};
        CHECK(!number(text, &value) && value == 73);
    }

    const struct {
        const char *text;
        uint64_t expected;
    } valid[] = {{"0", 0},
                 {"1", 1},
                 {"010", 8},
                 {"0xff", 255},
                 {"0XFF", 255},
                 {"18446744073709551615", UINT64_MAX},
                 {"0xffffffffffffffff", UINT64_MAX}};

    for (unsigned i = 0; i < sizeof(valid) / sizeof(*valid); ++i)
        CHECK(number(valid[i].text, &value) && value == valid[i].expected);
    puts("PASS 139 unsigned numeric parsing cases without failed-output mutation");
}

int main(void) {
    RUN(numeric_arguments);
    return 0;
}
