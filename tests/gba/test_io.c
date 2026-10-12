/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

static struct CupidGBA* fresh(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba != NULL);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void reserved_bits_do_not_enter_register_state(void) {
    struct CupidGBA* gba = fresh();
    static const struct { uint32_t address; uint16_t mask; } registers[] = {
        {REG_BG0CNT, 0xDFFF}, {REG_BG1CNT, 0xDFFF},
        {REG_WININ, 0x3F3F}, {REG_WINOUT, 0x3F3F},
        {REG_BLDCNT, 0x3FFF}, {REG_BLDALPHA, 0x1F1F},
        {REG_SOUNDCNT_L, 0xFF77}, {REG_SOUNDCNT_H, 0x770F}
    };
    for (unsigned i = 0; i < sizeof(registers) / sizeof(registers[0]); i++) {
        uint32_t address = GBA_BASE_IO + registers[i].address;
        cupid_memory_store16(&gba->cpu, address, 0, NULL);
        cupid_memory_store8(&gba->cpu, address, 0xFF, NULL);
        CHECK(cupid_memory_load16(&gba->cpu, address, NULL) == (registers[i].mask & 255));
        cupid_memory_store8(&gba->cpu, address + 1, 0xFF, NULL);
        CHECK(cupid_memory_load16(&gba->cpu, address, NULL) == registers[i].mask);
        CHECK(gba->memory.io[registers[i].address >> 1] == registers[i].mask);
    }
    CHECK(gba->video.bg[0].cnt == 0xDFFF && gba->video.bg[1].cnt == 0xDFFF);
    CHECK(gba->video.win.winin == 0x3F3F && gba->video.win.winout == 0x3F3F);
    CHECK(gba->video.blend.bldcnt == 0x3FFF && gba->video.blend.bldalpha == 0x1F1F);
    CHECK(gba->audio.soundcntL == 0xFF77 && gba->audio.soundcntH == 0x770F);
    cupid_memory_store32(&gba->cpu, GBA_BASE_IO + REG_BG0CNT, -1, NULL);
    CHECK((uint32_t)cupid_memory_load32(&gba->cpu, GBA_BASE_IO + REG_BG0CNT, NULL) == 0xDFFFDFFF);
    cupid_memory_store32(&gba->cpu, GBA_BASE_IO + REG_BLDCNT, -1, NULL);
    CHECK((uint32_t)cupid_memory_load32(&gba->cpu, GBA_BASE_IO + REG_BLDCNT, NULL) == 0x1F1F3FFF);
    destroy(gba);
}

static void cpu_loads_write_only_and_unmapped_io(void) {
    static const uint32_t offsets[] = {REG_BG0HOFS, 0x04E, 0x056, 0x08C,
        REG_FIFO_A_LO, REG_FIFO_B_LO, REG_DMA0SAD_LO, REG_DMA3DAD_LO, 0x0E0, 0x3FC};
    static const uint32_t arm[] = {0xE5D01000, 0xE1D010B0, 0xE5901000};
    static const uint16_t thumb[] = {0x7801, 0x8801, 0x6801};
    static const uint32_t expected[] = {0xAD, 0xDEAD, 0xDEADDEAD};
    for (unsigned mode = 0; mode < 2; mode++) {
        for (unsigned width = 0; width < 3; width++) {
            for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
                struct CupidGBA* gba = fresh();
                /* Both prefetched slots contain the independent probe value. */
                const uint32_t data = 0xDEADDEAD;
                for (unsigned word = 0; word < 4; word++)
                    memcpy(gba->memory.iwram + word * 4, &data, 4);
                gba->cpu.gprs[0] = GBA_BASE_IO + offsets[i];
                gba->memory.io[offsets[i] >> 1] = 0x2468;
                _CupidSetMode(&gba->cpu, mode ? MODE_THUMB : MODE_ARM);
                gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
                if (mode) {
                    memcpy(gba->memory.iwram, &thumb[width], 2);
                    CupidThumbWritePC(&gba->cpu);
                    cupid_thumb_step(&gba->cpu);
                } else {
                    memcpy(gba->memory.iwram, &arm[width], 4);
                    CupidARMWritePC(&gba->cpu);
                    cupid_arm_step(&gba->cpu);
                }
                CHECK((uint32_t)gba->cpu.gprs[1] == expected[width]);
                destroy(gba);
            }
        }
    }
}

static void open_bus_lanes_and_dma_latch(void) {
    struct CupidGBA* gba = fresh();
    gba->cpu.prefetch[1] = 0x12345678;
    CHECK(cupid_memory_load8(&gba->cpu, GBA_BASE_IO + REG_FIFO_A_LO + 3, NULL) == 0x12);
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_IO + REG_DMA3SAD_HI, NULL) == 0x1234);
    CHECK((uint32_t)cupid_memory_load32(&gba->cpu, GBA_BASE_IO + 0x0E0, NULL) == 0x12345678);
    gba->memory.lastWasDMA = true;
    gba->memory.dmaBus = 0x89ABCDEF;
    CHECK(cupid_memory_load8(&gba->cpu, GBA_BASE_IO + REG_FIFO_B_LO + 1, NULL) == 0xCD);
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_IO + REG_DMA0SAD_HI, NULL) == 0x89AB);
    CHECK((uint32_t)cupid_memory_load32(&gba->cpu, GBA_BASE_IO + 0x0E0, NULL) == 0x89ABCDEF);
    /* A timed CPU access completes the DMA handover. */
    gba->cpu.memory.timed = true;
    int cycles = 0;
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_IO + REG_FIFO_A_LO, &cycles) == 0xCDEF);
    CHECK(!gba->memory.lastWasDMA);
    gba->cpu.memory.timed = false;
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_IO + REG_FIFO_A_LO, NULL) == 0x5678);
    destroy(gba);
}

static void zero_holes_and_fifo_reset_strobes(void) {
    struct CupidGBA* gba = fresh();
    static const uint32_t zero[] = {0x066, 0x06A, 0x06E, 0x076, 0x07A,
        0x07E, 0x086, 0x08A, REG_DMA0CNT_LO, REG_DMA3CNT_LO};
    gba->cpu.prefetch[1] = 0xDEADDEAD;
    for (unsigned i = 0; i < sizeof(zero) / sizeof(zero[0]); i++) {
        gba->memory.io[zero[i] >> 1] = 0xFFFF;
        CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_IO + zero[i], NULL) == 0);
        CHECK(cupid_memory_load8(&gba->cpu, GBA_BASE_IO + zero[i] + 1, NULL) == 0);
    }
    cupid_memory_store16(&gba->cpu, GBA_BASE_IO + REG_SOUNDCNT_H, 0x8800, NULL);
    CHECK(cupid_io_read(gba, REG_SOUNDCNT_H) == 0);
    cupid_audio_fifo_write(&gba->audio.chA, 0x44332211);
    cupid_audio_fifo_write(&gba->audio.chB, 0x88776655);
    cupid_memory_store8(&gba->cpu, GBA_BASE_IO + REG_SOUNDCNT_H, 0x0F, NULL);
    CHECK(gba->audio.chA.fifoWrite == 1 && gba->audio.chB.fifoWrite == 1);
    CHECK(cupid_io_read(gba, REG_SOUNDCNT_H) == 0x000F);
    destroy(gba);
}

int main(void) {
    RUN(reserved_bits_do_not_enter_register_state);
    RUN(cpu_loads_write_only_and_unmapped_io);
    RUN(open_bus_lanes_and_dma_latch);
    RUN(zero_holes_and_fifo_reset_strobes);
    return 0;
}
