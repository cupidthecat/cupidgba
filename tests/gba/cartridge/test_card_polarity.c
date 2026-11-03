/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_card_polarity.c
 * @brief Reader brightness endpoints, packed pixel order, block thresholds and DMA payloads.
 *
 * - Derive expected bytes from white pixels and the documented last-byte-first scanline order.
 * - Keep normalized optical input separate from the camera's bus-visible brightness bits.
 */
#include "../../test.h"
#include "cupid/gba.h"

static const uint32_t control0 = 0x0E00FFB0, control1 = 0x0E00FFB1;
static const uint64_t epochs[] = {0, INT32_MAX - 1000u, UINT32_MAX - 1000u, UINT64_C(0x300000000)};

static struct CupidGBA *machine(unsigned epoch) {
    struct CupidGBA *gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = epochs[epoch];
    gba->timing.masterCycles = (uint32_t)epochs[epoch];
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    gba->memory.rom = calloc(1, 4096);
    CHECK(gba->memory.rom);
    gba->memory.romSize = gba->memory.romImageSize = 4096;
    gba->memory.romMask = 4095;
    cupid_cartridge_enable_card_reader(gba, true);
    cupid_io_write(gba, REG_WAITCNT, 0x1800);
    cupid_memory_store8(&gba->cpu, control0, 0x70, NULL);
    return gba;
}

static void destroy(struct CupidGBA *gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void surface(struct CupidGBA *gba, unsigned ink) {
    struct CupidCardReader *reader = &gba->cartridge.cardReader;
    if (!reader->dots)
        reader->dots = malloc(CUPID_CARD_DOTS);
    CHECK(reader->dots);
    memset(reader->dots, (int)ink, CUPID_CARD_DOTS);
}

static void boundary(struct CupidGBA *gba, unsigned block, unsigned level) {
    cupid_memory_store8(&gba->cpu, 0x0E00FF80 + block, (uint8_t)level, NULL);
}

static void acquire(struct CupidGBA *gba, unsigned line, int x) {
    struct CupidCardReader *reader = &gba->cartridge.cardReader;
    /* Position a controlled normalized surface without changing the serial
     * register decoder or relying on a particular queued card format. */
    reader->scanY = line - 1;
    reader->scanX = x;
    memset(reader->sensor + 20, 0, 48 * sizeof(uint16_t));
    reader->sensor[68] = 127;
    cupid_memory_store8(&gba->cpu, control1, 0, NULL);
    CHECK(reader->scanY == line && (reader->control[1] & 2));
    CHECK(!reader->irqActive && !(gba->irq.pendingIF & IRQ_GAMEPAK));
}

static void white(uint8_t expected[40], unsigned pixel) {
    /* The first pixel is bit zero of byte 39. This byte oracle does not use
     * the implementation's halfword indexing or byte-swap expression. */
    expected[39 - pixel / 8] |= (uint8_t)(1u << (pixel % 8));
}

static void check_line(struct CupidGBA *gba, const uint8_t expected[40]) {
    for (unsigned byte = 0; byte < 40; byte++) {
        CHECK(cupid_memory_load8(&gba->cpu, 0x0DFC0000 + byte, NULL) == expected[byte]);
        CHECK(cupid_memory_load8(&gba->cpu, 0x0DFDFF00 + byte, NULL) == expected[byte]);
    }
    for (unsigned half = 0; half < 20; half++) {
        const unsigned offset = half * 2;
        const uint16_t value = expected[offset] | (uint16_t)expected[offset + 1] << 8;
        CHECK(cupid_memory_loadU16(&gba->cpu, 0x0DFC0000 + offset, NULL) == value);
    }
    CHECK(cupid_memory_load8(&gba->cpu, 0x0DFC0089, NULL) == 0);
}

static void endpoints(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++) {
        struct CupidGBA *gba = machine(epoch);
        for (unsigned ink = 0; ink < 2; ink++) {
            surface(gba, ink);
            for (unsigned level = 0; level < 128; level++) {
                for (unsigned block = 0; block < 48; block++)
                    boundary(gba, block, level);
                acquire(gba, 10, 0);
                uint8_t expected[40];
                memset(expected, !ink && level != 127 ? 255 : 0, sizeof(expected));
                check_line(gba, expected);
                for (unsigned block = 0; block < 48; block++)
                    CHECK(gba->cartridge.cardReader.sensor[20 + block] ==
                          (block < 8 && !ink ? 127 : 0));
                CHECK(gba->cartridge.cardReader.sensor[68] == (ink ? 0 : 127));
                CHECK(cupid_timing_global_time(&gba->timing) == epochs[epoch]);
                scenes++;
            }
        }
        /* No card is a white surface, with the same threshold endpoints. */
        free(gba->cartridge.cardReader.dots);
        gba->cartridge.cardReader.dots = NULL;
        for (unsigned level = 0; level < 128; level++) {
            for (unsigned block = 0; block < 48; block++)
                boundary(gba, block, level);
            acquire(gba, 10, 0);
            uint8_t expected[40];
            memset(expected, level != 127 ? 255 : 0, sizeof(expected));
            check_line(gba, expected);
            scenes++;
        }
        destroy(gba);
    }
    CHECK(scenes == 1536);
    printf("card polarity endpoint scenes=%u\n", scenes);
}

static void patterns(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++) {
        struct CupidGBA *gba = machine(epoch);
        for (unsigned phase = 0; phase < 3; phase++)
            for (unsigned selected = 0; selected < 320; selected++) {
                surface(gba, 1);
                const unsigned column = 16 + (selected + phase) / 3;
                gba->cartridge.cardReader.dots[column] = 0;
                uint8_t expected[40] = {0};
                for (unsigned pixel = 0; pixel < 320; pixel++)
                    if (16 + (pixel + phase) / 3 == column)
                        white(expected, pixel);
                acquire(gba, 10, (int)phase);
                check_line(gba, expected);
                CHECK(gba->cartridge.cardReader.sensor[68] == 0);
                scenes++;
            }
        destroy(gba);
    }
    CHECK(scenes == 3840);
    printf("card polarity pixel-position scenes=%u\n", scenes);
}

static void blocks(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++) {
        struct CupidGBA *gba = machine(epoch);
        surface(gba, 0);
        for (unsigned row = 0; row < CUPID_CARD_HEIGHT; row++)
            for (unsigned column = 0; column < CUPID_CARD_STRIDE; column++)
                gba->cartridge.cardReader.dots[row * CUPID_CARD_STRIDE + column] =
                    (uint8_t)((row ^ column) & 1);
        for (unsigned blockRow = 0; blockRow < 6; blockRow++) {
            const unsigned line = blockRow * 41 + 10;
            for (unsigned mask = 0; mask < 256; mask++) {
                for (unsigned column = 0; column < 8; column++)
                    boundary(gba, blockRow * 8 + column, mask & (1u << column) ? 127 : 0);
                uint8_t expected[40] = {0};
                for (unsigned pixel = 0; pixel < 320; pixel++) {
                    const bool ink = line < 130 && (((line - 10) / 3 ^ (16 + pixel / 3)) & 1);
                    if (!ink && !(mask & (1u << (7 - pixel / 40))))
                        white(expected, pixel);
                }
                acquire(gba, line, 0);
                check_line(gba, expected);
                for (unsigned block = 0; block < 48; block++)
                    CHECK(gba->cartridge.cardReader.sensor[20 + block] ==
                          (block / 8 == blockRow ? 127 : 0));
                CHECK(gba->cartridge.cardReader.sensor[68] == (line < 130 ? 0 : 127));
                scenes++;
            }
        }
        destroy(gba);
    }
    CHECK(scenes == 6144);
    printf("card polarity block-threshold scenes=%u\n", scenes);
}

static void dma(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned width = 2; width <= 4; width += 2)
            for (unsigned pattern = 0; pattern < 8; pattern++) {
                struct CupidGBA *gba = machine(epoch);
                surface(gba, 0);
                uint8_t expected[40] = {0};
                for (unsigned column = 0; column < CUPID_CARD_STRIDE; column++)
                    gba->cartridge.cardReader.dots[column] = (uint8_t)((column >> pattern) & 1);
                for (unsigned pixel = 0; pixel < 320; pixel++)
                    if (!(((16 + pixel / 3) >> pattern) & 1))
                        white(expected, pixel);
                acquire(gba, 10, 0);
                memset(gba->memory.iwram + 0xF0, 0xA5, 64);
                cupid_dma_write_sad(gba, 3, 0x0DFC0000);
                cupid_dma_write_dad(gba, 3, GBA_BASE_IWRAM + 0x100);
                cupid_dma_write_cnt_lo(gba, 3, 40 / width);
                cupid_dma_write_cnt(gba, 3, width == 4 ? 0x8400 : 0x8000);
                unsigned clocks = 0;
                while (gba->memory.dma[3].enable && clocks < 512) {
                    cupid_timing_tick(&gba->timing, 1);
                    clocks++;
                }
                CHECK(clocks < 512 && !gba->memory.dma[3].enable);
                CHECK(!memcmp(gba->memory.iwram + 0x100, expected, sizeof(expected)));
                for (unsigned byte = 0; byte < 64; byte++)
                    if (byte < 16 || byte >= 56)
                        CHECK(gba->memory.iwram[0xF0 + byte] == 0xA5);
                CHECK(gba->cartridge.cardReader.control[1] & 2);
                CHECK(!(gba->irq.pendingIF & IRQ_GAMEPAK));
                destroy(gba);
                scenes++;
            }
    CHECK(scenes == 64);
    printf("card polarity complete DMA scenes=%u\n", scenes);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "endpoints"))
        RUN(endpoints);
    else if (!strcmp(argv[1], "patterns"))
        RUN(patterns);
    else if (!strcmp(argv[1], "blocks"))
        RUN(blocks);
    else if (!strcmp(argv[1], "dma"))
        RUN(dma);
    else
        CHECK(false);
    return 0;
}
