/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file key-script.c
 * @brief Validate bounded input scripts and retain ordered frame/button pairs.
 */
#include "cupid/platform/key-script.h"
#include "cupid/file.h"
#include "cupid/gba.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_EVENTS = 65536, MAX_BYTES = 16 * 1024 * 1024, MAX_LINE = 255 };

static bool number(const char *text, uint64_t *value) {
    if (*text < '0' || *text > '9')
        return false;
    char *end;
    errno = 0;
    unsigned long long parsed = strtoull(text, &end, 0);
    if (errno || *end || parsed > UINT64_MAX)
        return false;
    *value = (uint64_t)parsed;
    return true;
}

static char *token(char **cursor) {
    while (isspace((unsigned char)**cursor))
        ++*cursor;
    char *start = *cursor;
    while (**cursor && !isspace((unsigned char)**cursor))
        ++*cursor;
    if (**cursor)
        *(*cursor)++ = 0;
    return start;
}

static bool line_events(char *line, struct CupidKeyEvent **events, unsigned *count,
                        unsigned *capacity) {
    char *comment = strchr(line, '#');
    if (comment)
        *comment = 0;
    char *cursor = line;
    const char *frameText = token(&cursor);
    if (!*frameText)
        return true;
    const char *keyText = token(&cursor);
    const char *extra = token(&cursor);
    uint64_t frame, keys;
    if (*extra || !number(frameText, &frame) || !number(keyText, &keys) || keys > 0x3FF ||
        (*count && frame < (*events)[*count - 1].frame) || *count == MAX_EVENTS)
        return false;
    if (*count == *capacity) {
        unsigned next = *capacity ? *capacity * 2 : 64;
        struct CupidKeyEvent *grown = realloc(*events, (size_t)next * sizeof(*grown));
        if (!grown)
            return false;
        *events = grown;
        *capacity = next;
    }
    (*events)[(*count)++] = (struct CupidKeyEvent){frame, (uint16_t)keys};
    return true;
}

bool cupid_platform_read_key_script(const char *path, struct CupidKeyEvent **events,
                                    unsigned *count) {
    *events = NULL;
    *count = 0;
    if (!cupid_file_is_regular(path))
        return false;
    FILE *file = fopen(path, "rb");
    if (!file) {
        perror(path);
        return false;
    }
    struct CupidKeyEvent *loaded = NULL;
    unsigned total = 0, capacity = 0, lineNumber = 1, length = 0;
    size_t bytes = 0;
    char line[MAX_LINE + 1];
    bool valid = true;
    int ch;
    while ((ch = fgetc(file)) != EOF) {
        if (++bytes > MAX_BYTES || !ch) {
            valid = false;
            break;
        }
        if (ch == '\n') {
            line[length] = 0;
            if (!line_events(line, &loaded, &total, &capacity)) {
                valid = false;
                break;
            }
            length = 0;
            ++lineNumber;
        } else if (length == MAX_LINE) {
            valid = false;
            break;
        } else {
            line[length++] = (char)ch;
        }
    }
    line[length] = 0;
    if (valid && (ferror(file) || !line_events(line, &loaded, &total, &capacity)))
        valid = false;
    if (fclose(file))
        valid = false;
    if (!valid) {
        fprintf(stderr, "%s:%u: invalid key script\n", path, lineNumber);
        free(loaded);
        return false;
    }
    *events = loaded;
    *count = total;
    return true;
}

void cupid_platform_key_replay_apply(struct CupidGBA *gba, struct CupidKeyReplay *replay) {
    while (replay->next < replay->count && replay->events[replay->next].frame <= replay->frame) {
        gba->keysActive = replay->events[replay->next++].keys;
        cupid_gba_test_keypad_irq(gba);
    }
}

void cupid_platform_key_replay_advance(struct CupidGBA *gba, struct CupidKeyReplay *replay) {
    if (replay->frame != UINT64_MAX)
        ++replay->frame;
    cupid_platform_key_replay_apply(gba, replay);
}
