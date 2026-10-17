/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../test.h"
#include <inttypes.h>

static struct CupidGBA* machine(uint64_t epoch) {
    struct CupidGBA* g = calloc(1, sizeof(*g));
    CHECK(g);
    cupid_gba_create(g);
    g->memory.rom = calloc(1, 0x4000);
    CHECK(g->memory.rom);
    g->memory.romSize = g->memory.romImageSize = 0x4000;
    g->memory.romMask = 0x3FFF;
    const uint32_t code[] = {0xE1A00000, 0xEAFFFFFE};
    memcpy(g->memory.rom, code, sizeof(code));
    cupid_gba_reset(g);
    if (epoch) {
        /* Recreate the same video entry at a later scheduler epoch. No CPU
         * instruction or unrelated peripheral event runs in these wrap cases. */
        cupid_timing_clear(&g->timing);
        g->timing.globalCycles = epoch;
        g->timing.masterCycles = (uint32_t)epoch;
        g->cpu.cycles = 0;
        cupid_video_reset(&g->video);
        cupid_video_skip_bios(&g->video);
    }
    CHECK(cupid_timing_global_time(&g->timing) == epoch);
    CHECK(g->cpu.executionMode == MODE_ARM && g->cpu.privilegeMode == MODE_SYS);
    CHECK(cupid_arm_instruction_address(&g->cpu) == GBA_BASE_ROM0);
    cupid_io_write(g, REG_DISPSTAT, 0xA038);
    cupid_io_write(g, REG_IE, IRQ_VBLANK | IRQ_HBLANK | IRQ_VCOUNT);
    cupid_io_write(g, REG_IME, 1);
    return g;
}

static void check_clock(struct CupidGBA* g, uint64_t epoch, unsigned elapsed) {
    /* This is an independent line-position oracle. Fast entry begins on line
     * 126 at dot clock 841; the complete line remains 1232 clocks. */
    unsigned position = 126 * 1232 + 841 + elapsed;
    unsigned line = position / 1232 % 228;
    unsigned cycle = position % 1232;
    unsigned previous = (line + 227) % 228;
    unsigned flags = (line >= 160 && line < 227 ? 1u : 0u)
        | (cycle >= 1007 ? 2u : 0u)
        | ((cycle ? line == 160 : previous == 160) ? 4u : 0u);
    uint16_t pending = (elapsed >= 168 ? IRQ_HBLANK : 0)
        | (elapsed >= 41049 ? IRQ_VBLANK : 0)
        | (elapsed >= 41050 ? IRQ_VCOUNT : 0);
    CHECK(cupid_timing_global_time(&g->timing) == epoch + elapsed);
    CHECK(cupid_io_read(g, REG_VCOUNT) == line);
    CHECK(cupid_io_read(g, REG_DISPSTAT) == (0xA038 | flags));
    CHECK(g->irqf == pending && cupid_io_read(g, REG_IF) == pending);
    CHECK(g->irq.available == (elapsed >= 169));
    CHECK(g->irq.line == (elapsed >= 170));
    unsigned frames = elapsed < 41047 ? 0 : 1 + (elapsed - 41047) / 280896;
    CHECK(g->video.frameCounter == frames);
}

static void run(unsigned group) {
    static const uint64_t epochs[] = {0, UINT64_C(0xFFFFFFA0),
        UINT64_C(0x1000000A5), UINT64_C(0x10000FFF9)};
    CHECK(group < 4);
    uint64_t epoch = epochs[group];
    struct CupidGBA* fine = machine(epoch);
    struct CupidGBA* bulk = machine(epoch);
    const unsigned end = 2 * 280896;
    check_clock(fine, epoch, 0);
    for (unsigned elapsed = 1; elapsed <= end; ++elapsed) {
        CHECK(fine->cpu.cycles == 0);
        cupid_timing_tick(&fine->timing, 1);
        check_clock(fine, epoch, elapsed);
    }
    static const unsigned partitions[] = {1, 2, 3, 17, 1231, 4096};
    for (unsigned elapsed = 0, index = 0; elapsed < end; ++index) {
        unsigned clocks = partitions[index % 6];
        if (clocks > end - elapsed) clocks = end - elapsed;
        cupid_timing_tick(&bulk->timing, (int32_t)clocks);
        elapsed += clocks;
        check_clock(bulk, epoch, elapsed);
    }
    CHECK(!memcmp(fine->memory.io, bulk->memory.io, GBA_SIZE_IO));
    CHECK(!memcmp(fine->video.outputBuffer, bulk->video.outputBuffer,
        sizeof(fine->video.outputBuffer)));
    CHECK(fine->irq.logicalLine == bulk->irq.logicalLine);
    CHECK(fine->irq.logicalAvailable == bulk->irq.logicalAvailable);
    printf("PASS startup phase group=%u epoch=%" PRIu64
        ": 561793 complete status/IF/IRQ/frame observations and partition equality\n", group, epoch);
    cupid_gba_destroy(fine); free(fine);
    cupid_gba_destroy(bulk); free(bulk);
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    run((unsigned)strtoul(argv[1], NULL, 0));
    return 0;
}
