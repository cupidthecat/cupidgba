/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file peripherals.c
 * @brief Strict frontend peripheral parsing and core configuration.
 *
 * - Validate presence modes and external sensor sample ranges.
 * - Apply explicit device choices and samples to the selected cartridge.
 * - Validate and stage complete card files before transferring input ownership.
 */
#include "cupid/platform/peripherals.h"
#include "cupid/file.h"
#include "cupid/gba.h"
#include <errno.h>

bool cupid_platform_peripheral_mode(const char *text, int *presence) {
    if (!strcmp(text, "auto"))
        *presence = -1;
    else if (!strcmp(text, "off"))
        *presence = 0;
    else if (!strcmp(text, "on"))
        *presence = 1;
    else
        return false;
    return true;
}

bool cupid_platform_solar_level(const char *text, uint8_t *level) {
    if (!*text)
        return false;
    unsigned value = 0;
    for (const char *c = text; *c; c++) {
        if (*c < '0' || *c > '9')
            return false;
        value = value * 10 + (unsigned)(*c - '0');
        if (value > 255)
            return false;
    }
    *level = (uint8_t)value;
    return true;
}

bool cupid_platform_sample12(const char *text, uint16_t *sample) {
    if (!*text)
        return false;
    unsigned value = 0;
    for (const char *c = text; *c; c++) {
        if (*c < '0' || *c > '9')
            return false;
        value = value * 10 + (unsigned)(*c - '0');
        if (value > 4095)
            return false;
    }
    *sample = (uint16_t)value;
    return true;
}

bool cupid_platform_gyro_sample(const char *text, uint16_t *sample) {
    return cupid_platform_sample12(text, sample);
}

void cupid_platform_peripherals_configure(struct CupidGBA *gba,
                                          const struct CupidPlatformPeripherals *config) {
    if (config->solarPresence >= 0)
        cupid_cartridge_enable_solar(gba, config->solarPresence != 0);
    if (config->rumblePresence >= 0)
        cupid_cartridge_enable_rumble(gba, config->rumblePresence != 0);
    if (config->gyroPresence >= 0)
        cupid_cartridge_enable_gyro(gba, config->gyroPresence != 0);
    if (config->tiltPresence >= 0)
        cupid_cartridge_enable_tilt(gba, config->tiltPresence != 0);
    if (config->cardReaderPresence >= 0)
        cupid_cartridge_enable_card_reader(gba, config->cardReaderPresence != 0);
    cupid_cartridge_set_solar_level(gba, config->solarLevel);
    cupid_cartridge_set_gyro_sample(gba, config->gyroSample);
    cupid_cartridge_set_tilt_sample(gba, config->tiltX, config->tiltY);
}

static bool queue_card_file(struct CupidCardReader *input, const char *path) {
    enum { MAX_CARD_FILE = CUPID_CARD_QUEUE * 2912 };

    if (!cupid_file_is_regular(path))
        return false;
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;
    uint8_t data[MAX_CARD_FILE];
    bool valid = fseek(file, 0, SEEK_END) == 0;
    const long length = valid ? ftell(file) : -1;
    valid = valid && length > 0 && length <= MAX_CARD_FILE && fseek(file, 0, SEEK_SET) == 0;
    if (valid) {
        valid = fread(data, 1, (size_t)length, file) == (size_t)length;
        if (valid)
            valid = fgetc(file) == EOF && !ferror(file);
    }
    if (fclose(file) != 0)
        valid = false;
    if (!valid) {
        errno = EINVAL;
        return false;
    }
    const size_t size = (size_t)length;
    size_t strip = size;
    /* Conventional multi-strip RAW/BIN files identify their strip geometry
     * in the first header. Reduced headers and bitmap files are single strips. */
    if (size != 1308 && size != 1344 && size != 1872 && size != 2076 && size != 2112 &&
        size != 2912 && size != 3520 && size != 5456) {
        if (size >= 2 && data[0] == 0 && (data[1] == 2 || data[1] == 3))
            strip = data[1] == 2 ? 1872 : 2912;
        else if (size >= 48 && data[0] == 0 && data[1] == 0x30 && (data[14] == 1 || data[14] == 2))
            strip = data[14] == 2 ? 1344 : 2112;
    }
    if (size % strip || size / strip > CUPID_CARD_QUEUE - input->queueCount) {
        errno = EINVAL;
        return false;
    }
    for (size_t offset = 0; offset < size; offset += strip)
        if (!cupid_card_reader_queue(input, data + offset, strip)) {
            errno = EINVAL;
            return false;
        }
    return true;
}

bool cupid_platform_cards_read(struct CupidCardReader *input, const char *const paths[],
                               unsigned count) {
    if (!input || count > CUPID_CARD_QUEUE || (count && !paths)) {
        errno = EINVAL;
        return false;
    }
    struct CupidCardReader staged;
    cupid_card_reader_init(&staged);
    for (unsigned i = 0; i < count; i++) {
        if (!paths[i])
            errno = EINVAL;
        if (!paths[i] || !queue_card_file(&staged, paths[i])) {
            const int failure = errno ? errno : EINVAL;
            cupid_card_reader_deinit(&staged);
            errno = failure;
            return false;
        }
    }
    cupid_card_reader_deinit(input);
    *input = staged;
    return true;
}

bool cupid_platform_cards_attach(struct CupidGBA *gba, struct CupidCardReader *input) {
    if (!gba || !input) {
        errno = EINVAL;
        return false;
    }
    if (!input->queueCount)
        return true;
    struct CupidCardReader *reader = &gba->cartridge.cardReader;
    if (!gba->cartridge.cardReaderPresent || !gba->memory.rom ||
        input->queueCount > CUPID_CARD_QUEUE - reader->queueCount || input == reader) {
        errno = EINVAL;
        return false;
    }
    while (input->queueCount) {
        const unsigned slot = (reader->queueHead + reader->queueCount) % CUPID_CARD_QUEUE;
        reader->queued[slot] = input->queued[input->queueHead];
        input->queued[input->queueHead] = NULL;
        input->queueHead = (input->queueHead + 1) % CUPID_CARD_QUEUE;
        --input->queueCount;
        ++reader->queueCount;
    }
    return true;
}
