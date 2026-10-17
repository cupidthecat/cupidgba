/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../../test.h"

struct TransferCase {
    unsigned mode;
    int start;
    unsigned nonsequential;
    int data[4];
};

/* Addresses are transfer-width units relative to a 128 KiB page. Each mask
 * lists the four cartridge strobes, oldest first in bit zero. The data columns
 * distinguish address latching from sequential cartridge counting. */
static const struct TransferCase cases[] = {
    {GBA_DMA_INCREMENT, -2, 0x5, {-2, -1, 0, 1}},
    {GBA_DMA_DECREMENT,  0, 0x5, {0, 1, -2, -1}},
    {GBA_DMA_FIXED,     -1, 0xF, {-1, -1, -1, -1}},
    {GBA_DMA_FIXED,     -2, 0x1, {-2, -1, 0, 1}},
    {GBA_DMA_DECREMENT,  1, 0x9, {1, 2, 3, -2}},
};

static const uint64_t origins[] = {
    0, INT32_MAX - 2u, UINT32_MAX - 2u, UINT64_C(0x300000000)
};
static const uint32_t banks[] = {GBA_BASE_ROM0, GBA_BASE_ROM1, GBA_BASE_ROM2};
static unsigned observations;

static uint16_t halfword(uint32_t offset) {
    return (uint16_t)(0x5A5Au ^ (offset >> 1) ^ ((offset >> 17) * 0x71B9u));
}

static uint32_t payload(uint32_t offset, unsigned width) {
    uint32_t value = halfword(offset);
    if (width == 4) value |= (uint32_t)halfword(offset + 2) << 16;
    return value;
}

static struct CupidGBA* machine(uint64_t origin, bool fast) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.masterCycles = (uint32_t)origin;
    gba->timing.globalCycles = origin;
    gba->audio.lastSample = origin;
    gba->audio.sampleOrigin = origin;
    gba->audio.nextSample = origin + 512;
    gba->memory.rom = calloc(1, 0x40000);
    CHECK(gba->memory.rom);
    gba->memory.romSize = gba->memory.romImageSize = 0x40000;
    gba->memory.romMask = 0x3FFFF;
    for (uint32_t offset = 0x1FFE0; offset < 0x20040; offset += 2) {
        uint16_t value = halfword(offset);
        memcpy(gba->memory.rom + offset, &value, sizeof(value));
    }
    /* The second profile selects three-clock N and two-clock S halfwords in
     * all banks. The default profile also covers N == S and N < S words. */
    if (fast) cupid_memory_update_waitstates(gba, 0x06D8);
    return gba;
}

static void advance(struct CupidGBA* gba, unsigned clocks, bool punctual) {
    if (punctual) {
        for (unsigned clock = 0; clock < clocks; clock++) cupid_timing_tick(&gba->timing, 1);
    } else {
        cupid_timing_tick(&gba->timing, (int32_t)clocks);
    }
}

static uint32_t destination(struct CupidGBA* gba, unsigned index, unsigned width) {
    uint32_t address = GBA_BASE_IWRAM + 0x100 + index * width;
    return width == 4 ? cupid_memory_load32(&gba->cpu, address, NULL)
                      : cupid_memory_loadU16(&gba->cpu, address, NULL);
}

static void pages_follow_the_preceding_programmed_strobe(unsigned width) {
    const unsigned defaultS[] = {3, 5, 9};
    for (unsigned epoch = 0; epoch < 4; epoch++) for (unsigned bank = 0; bank < 3; bank++) {
        for (unsigned fast = 0; fast < 2; fast++) for (unsigned punctual = 0; punctual < 2; punctual++) {
            for (unsigned test = 0; test < sizeof(cases) / sizeof(*cases); test++) {
                const struct TransferCase* transfer = &cases[test];
                struct CupidGBA* gba = machine(origins[epoch], fast != 0);
                unsigned s = fast ? 2 : defaultS[bank];
                unsigned n = fast ? 3 : 5;
                unsigned units = width / 2;
                uint32_t source = banks[bank] + (uint32_t)(0x20000 + transfer->start * (int)width);
                uint32_t target = GBA_BASE_IWRAM + 0x100;
                cupid_dma_write_sad(gba, 3, source);
                cupid_dma_write_dad(gba, 3, target);
                cupid_dma_write_cnt_lo(gba, 3, 4);
                cupid_dma_write_cnt(gba, 3, (uint16_t)(0xC000 | (width == 4 ? 0x400 : 0) | (transfer->mode << 7)));
                unsigned elapsed = 0;
                unsigned deadline = 3; /* Trigger acceptance plus bus handover. */
                for (unsigned beat = 0; beat < 4; beat++) {
                    bool nonsequential = (transfer->nonsequential & (1u << beat)) != 0;
                    unsigned read = (nonsequential ? n : s) + (units - 1) * s;
                    deadline += read + 1;
                    advance(gba, deadline - elapsed - 1, punctual != 0);
                    uint32_t before = destination(gba, beat, width);
                    if (before) fprintf(stderr,
                        "early page write width=%u case=%u beat=%u bank=%u fast=%u clock=%" PRIu64 " required=%u\n",
                        width, test, beat, bank, fast,
                        cupid_timing_global_time(&gba->timing) - origins[epoch], deadline);
                    CHECK(before == 0);
                    CHECK(gba->irqf == 0);
                    advance(gba, 1, punctual != 0);
                    uint32_t offset = (uint32_t)(0x20000 + transfer->data[beat] * (int)width);
                    uint32_t expected = payload(offset, width);
                    uint32_t actual = destination(gba, beat, width);
                    if (actual != expected) fprintf(stderr,
                        "page data width=%u case=%u beat=%u bank=%u fast=%u observed=%08x expected=%08x\n",
                        width, test, beat, bank, fast, actual, expected);
                    CHECK(actual == expected);
                    CHECK(cupid_timing_global_time(&gba->timing) == origins[epoch] + deadline);
                    elapsed = deadline;
                }
                const struct CupidDMA* dma = &gba->memory.dma[3];
                CHECK(!dma->enable && gba->memory.dmaRunning && gba->irqf == 0);
                CHECK(dma->busyUntil == origins[epoch] + deadline);
                int step = transfer->mode == GBA_DMA_INCREMENT ? 1 : transfer->mode == GBA_DMA_DECREMENT ? -1 : 0;
                CHECK(dma->nextSource == source + (uint32_t)(step * 4 * (int)width));
                CHECK(dma->nextDest == target + 4 * width && dma->nextCount == 0);
                advance(gba, 1, punctual != 0);
                CHECK(!gba->memory.dmaRunning && gba->irqf == IRQ_DMA3);
                for (unsigned beat = 0; beat < 4; beat++) {
                    uint32_t offset = (uint32_t)(0x20000 + transfer->data[beat] * (int)width);
                    CHECK(destination(gba, beat, width) == payload(offset, width));
                }
                observations++;
                cupid_gba_destroy(gba);
                free(gba);
            }
        }
    }
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "halfword")) pages_follow_the_preceding_programmed_strobe(2);
    else if (!strcmp(argv[1], "word")) pages_follow_the_preceding_programmed_strobe(4);
    else return 2;
    printf("PASS %u complete DMA page transactions\n", observations);
    CHECK(observations == 240);
    return 0;
}
