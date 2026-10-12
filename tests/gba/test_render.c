/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../test.h"

static struct CupidGBA* machine(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void green_swap_exchanges_only_adjacent_green_components(void) {
    struct CupidGBA* gba = machine();
    cupid_timing_clear(&gba->timing);
    cupid_video_reset(&gba->video);
    const uint16_t colors[] = {0x001F, 0x03E0, 0x7C00, 0x001F};
    for (unsigned i = 0; i < 240 * 160; i++) gba->video.vram[i] = colors[i % 4];
    cupid_io_write(gba, REG_DISPCNT, 0x0403);
    cupid_timing_tick(&gba->timing, 3 * 1232);
    cupid_io_write(gba, REG_GREENSWAP, 0xFFFF);
    CHECK(cupid_io_read(gba, REG_GREENSWAP) == 1);
    cupid_timing_tick(&gba->timing, 61);
    uint32_t* row = gba->video.outputBuffer + 3 * 240;
    CHECK(row[0] == color16_to_rgba(0x03FF));
    CHECK(row[1] == color16_to_rgba(0));
    CHECK(row[2] == color16_to_rgba(0x7C00));
    CHECK(row[3] == color16_to_rgba(0x001F));
    for (unsigned i = 0; i < 4; i++) CHECK(gba->video.scanline[i] == colors[i]);
    cupid_io_write(gba, REG_GREENSWAP, 0);
    cupid_timing_tick(&gba->timing, 1232);
    for (unsigned i = 0; i < 4; i++) CHECK(row[240 + i] == color16_to_rgba(colors[i]));
    destroy(gba);
}

static void register_changes_affect_only_later_scanout(void) {
    struct CupidGBA* gba = machine();
    cupid_timing_clear(&gba->timing);
    cupid_video_reset(&gba->video);
    const uint16_t colors[] = {0x001F, 0x03E0, 0x7C00};
    for (unsigned i = 0; i < GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT; i++) {
        gba->video.vram[i] = colors[i % 3];
    }
    cupid_io_write(gba, REG_DISPCNT, 0x0403);
    /* Allow display-enable latches to settle before observing line 3. */
    cupid_timing_tick(&gba->timing, 3 * 1232);
    cupid_io_write(gba, REG_GREENSWAP, 1);
    cupid_timing_tick(&gba->timing, 100);
    uint32_t* row = gba->video.outputBuffer + 3 * GBA_SCREEN_WIDTH;
    CHECK(row[0] == color16_to_rgba(0x03FF));
    CHECK(row[1] == color16_to_rgba(0));
    cupid_io_write(gba, REG_GREENSWAP, 0);
    cupid_timing_tick(&gba->timing, 1007 - 100);
    CHECK(row[0] == color16_to_rgba(0x03FF));
    CHECK(row[1] == color16_to_rgba(0));
    CHECK(row[24] == color16_to_rgba(0x001F));
    CHECK(row[25] == color16_to_rgba(0x03E0));
    destroy(gba);
}

static struct CupidGBA* bitmap(unsigned mode) {
    struct CupidGBA* gba = machine();
    cupid_timing_clear(&gba->timing);
    cupid_video_reset(&gba->video);
    cupid_io_write(gba, REG_BG2PD, 0);
    cupid_io_write(gba, REG_DISPCNT, (uint16_t)(0x0400 | mode));
    return gba;
}

static void fetches_and_palette_reads_retain_their_values(void) {
    struct CupidGBA* gba = bitmap(3);
    gba->video.vram[0] = 0x001F;
    gba->video.vram[1] = 0x03E0;
    cupid_timing_tick(&gba->timing, 3 * 1232 + 36);
    cupid_memory_store32(&gba->cpu, GBA_BASE_VRAM, 0x7C007C00, NULL);
    cupid_timing_tick(&gba->timing, 16);
    uint32_t* row = gba->video.outputBuffer + 3 * 240;
    CHECK(row[0] == 0 && row[1] == 0); /* Both pixels leave the merge unit together. */
    cupid_timing_tick(&gba->timing, 1);
    CHECK(row[0] == color16_to_rgba(0x001F) && row[1] == color16_to_rgba(0x7C00));
    destroy(gba);

    gba = bitmap(4);
    gba->video.vram[0] = 0x0101;
    gba->video.palette[1] = 0x001F;
    cupid_timing_tick(&gba->timing, 3 * 1232 + 47);
    cupid_memory_store16(&gba->cpu, GBA_BASE_PALETTE + 2, 0x7C00, NULL);
    cupid_timing_tick(&gba->timing, 6);
    row = gba->video.outputBuffer + 3 * 240;
    CHECK(row[0] == color16_to_rgba(0x001F) && row[1] == color16_to_rgba(0x7C00));
    destroy(gba);
}

static void affine_steps_and_reference_writes_keep_separate_latches(void) {
    struct CupidGBA* gba = bitmap(3);
    for (unsigned x = 0; x < 240; x++) gba->video.vram[x] = x & 31;
    cupid_timing_tick(&gba->timing, 3 * 1232 + 50);
    cupid_io_write(gba, REG_BG2PA, 0x0200);
    cupid_io_write(gba, REG_BG2X_LO, 20 << 8);
    cupid_timing_tick(&gba->timing, 1232 + 100 - 50);
    uint32_t* row = gba->video.outputBuffer + 3 * 240;
    CHECK(row[3] == color16_to_rgba(3));
    CHECK(row[4] == color16_to_rgba(4));
    CHECK(row[5] == color16_to_rgba(6));
    CHECK(row[240] == color16_to_rgba(20));
    CHECK(row[241] == color16_to_rgba(22));
    destroy(gba);

    gba = bitmap(3);
    cupid_timing_tick(&gba->timing, 3 * 1232 + 1100);
    CHECK(gba->video.bg[2].sy == 0);
    cupid_io_write(gba, REG_BG2PD, 0x0300);
    cupid_timing_tick(&gba->timing, 131);
    CHECK(gba->video.bg[2].sy == 0);
    cupid_timing_tick(&gba->timing, 1);
    CHECK(gba->video.bg[2].sy == 0x0300);
    destroy(gba);
}

static struct CupidGBA* sprite_scene(unsigned effect, bool first, bool second, bool semi, bool window) {
    struct CupidGBA* gba = bitmap(3);
    for (unsigned i = 1; i < 128; i++) gba->video.oam.obj[i].a = 0x0200;
    gba->video.oam.obj[0] = (struct CupidOBJ){.a = semi ? 0x0400 : 0, .c = 512};
    for (unsigned i = 0; i < 16; i++) gba->video.vram[0x14000 / 2 + i] = 0x1111;
    for (unsigned i = 0; i < 240; i++) gba->video.vram[i] = 0x4000;
    gba->video.palette[257] = 0x0010;
    cupid_io_write(gba, REG_BG2CNT, 1);
    cupid_io_write(gba, REG_BLDCNT, (uint16_t)((effect << 6) | (first ? 16 : 0) | (second ? 0x0400 : 0x2000)));
    cupid_io_write(gba, REG_BLDALPHA, 0x0808);
    cupid_io_write(gba, REG_BLDY, 8);
    if (window) {
        cupid_io_write(gba, REG_WIN0H, 240);
        cupid_io_write(gba, REG_WIN0V, 0x01A0);
        cupid_io_write(gba, REG_WININ, 0x0014);
    }
    cupid_io_write(gba, REG_DISPCNT, (uint16_t)(0x1443 | (window ? 0x2000 : 0)));
    return gba;
}

static void effects_use_the_two_visible_layers_and_window_control(void) {
    for (unsigned semi = 0; semi < 2; semi++) {
        for (unsigned effect = BLEND_BRIGHTEN; effect <= BLEND_DARKEN; effect++) {
            struct CupidGBA* gba = sprite_scene(effect, true, false, semi != 0, false);
            cupid_timing_tick(&gba->timing, 3 * 1232 + 1007);
            unsigned expected = effect == BLEND_BRIGHTEN ? (23 | (15 << 5) | (15 << 10)) : 8;
            CHECK(gba->video.outputBuffer[3 * 240] == color16_to_rgba((uint16_t)expected));
            destroy(gba);
        }
    }
    struct CupidGBA* gba = sprite_scene(BLEND_ALPHA, true, false, true, false);
    cupid_timing_tick(&gba->timing, 3 * 1232 + 1007);
    CHECK(gba->video.outputBuffer[3 * 240] == color16_to_rgba(0x0010)); /* BG2 blocks the targeted backdrop. */
    destroy(gba);
    gba = sprite_scene(BLEND_NONE, false, true, true, true);
    cupid_timing_tick(&gba->timing, 3 * 1232 + 1007);
    CHECK(gba->video.outputBuffer[3 * 240] == color16_to_rgba(0x2008));
    destroy(gba);
    gba = sprite_scene(BLEND_BRIGHTEN, true, false, true, true);
    cupid_timing_tick(&gba->timing, 3 * 1232 + 1007);
    CHECK(gba->video.outputBuffer[3 * 240] == color16_to_rgba(0x0010));
    destroy(gba);
}

static void display_memory_beats_wait_for_the_fetch_unit(void) {
    struct CupidGBA* gba = bitmap(3);
    gba->cpu.memory.timed = true;
    cupid_timing_tick(&gba->timing, 3 * 1232 + 34);
    int cycles = 0;
    uint64_t start = cupid_timing_global_time(&gba->timing);
    cupid_memory_store32(&gba->cpu, GBA_BASE_VRAM, 0x12345678, &cycles);
    CHECK(cupid_timing_global_time(&gba->timing) - start == 3); /* Fetch at 35, then two available beats. */
    CHECK(gba->video.vram[0] == 0x5678 && gba->video.vram[1] == 0x1234);
    CHECK(!gba->memory.busBusy);
    destroy(gba);

    gba = bitmap(4);
    gba->cpu.memory.timed = true;
    cupid_timing_tick(&gba->timing, 3 * 1232 + 46);
    cycles = 0;
    start = cupid_timing_global_time(&gba->timing);
    cupid_memory_store8(&gba->cpu, GBA_BASE_PALETTE + 2, 0x3A, &cycles);
    CHECK(cupid_timing_global_time(&gba->timing) - start == 2);
    CHECK(gba->video.palette[1] == 0x3A3A && !gba->memory.busBusy);
    destroy(gba);
}

static void dma_word_writes_publish_each_display_bus_beat(void) {
    struct CupidGBA* gba = bitmap(3);
    gba->video.vram[0] = 0x001F;
    gba->video.vram[1] = 0x03E0;
    cupid_memory_store32(&gba->cpu, GBA_BASE_IWRAM, 0x7C007C00, NULL);
    cupid_timing_tick(&gba->timing, 3 * 1232 + 30);
    cupid_dma_write_sad(gba, 0, GBA_BASE_IWRAM);
    cupid_dma_write_dad(gba, 0, GBA_BASE_VRAM);
    cupid_dma_write_cnt_lo(gba, 0, 1);
    cupid_dma_write_cnt(gba, 0, 0x8400);
    cupid_timing_tick(&gba->timing, 5);
    CHECK(gba->video.vram[0] == 0x001F && gba->video.vram[1] == 0x03E0);
    cupid_timing_tick(&gba->timing, 1);
    CHECK(gba->video.vram[0] == 0x7C00 && gba->video.vram[1] == 0x03E0);
    cupid_timing_tick(&gba->timing, 1);
    CHECK(gba->video.vram[1] == 0x7C00 && gba->memory.dma[0].busyUntil == 3 * 1232 + 37);
    cupid_timing_tick(&gba->timing, 16);
    CHECK(!gba->memory.dmaRunning);
    uint32_t* row = gba->video.outputBuffer + 3 * 240;
    CHECK(row[0] == color16_to_rgba(0x001F) && row[1] == color16_to_rgba(0x7C00));
    destroy(gba);
}

static void object_attribute_fetches_are_retained_for_the_next_line(void) {
    struct CupidGBA* gba = sprite_scene(BLEND_NONE, false, false, false, false);
    gba->video.oam.obj[0].b = 8;
    cupid_timing_tick(&gba->timing, 3 * 1232 + 50);
    cupid_memory_store16(&gba->cpu, GBA_BASE_OAM + 2, 48, NULL);
    cupid_timing_tick(&gba->timing, 2 * 1232 + 1007 - 50);
    CHECK(gba->video.outputBuffer[3 * 240 + 8] == color16_to_rgba(0x0010));
    CHECK(gba->video.outputBuffer[4 * 240 + 8] == color16_to_rgba(0x0010));
    CHECK(gba->video.outputBuffer[5 * 240 + 48] == color16_to_rgba(0x0010));
    CHECK(gba->video.outputBuffer[5 * 240 + 8] == color16_to_rgba(0x4000));
    destroy(gba);
}

struct RasterWrite {
    struct CupidGBA* gba;
    uint32_t address;
    uint16_t value;
};

static void raster_write(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing;
    (void)late;
    struct RasterWrite* write = context;
    cupid_memory_store16(&write->gba->cpu, write->address, (int16_t)write->value, NULL);
}

static void late_dispatch_preserves_pixels_across_clock_wrap(void) {
    const uint64_t clocks[] = {0, UINT32_MAX - 1000u, UINT64_C(0x300000000)};
    for (unsigned c = 0; c < 3; c++) {
        struct CupidGBA* machines[] = {machine(), machine()};
        struct RasterWrite writes[2][4];
        struct CupidTimingEvent events[2][4];
        for (unsigned i = 0; i < 2; i++) {
            struct CupidGBA* gba = machines[i];
            cupid_timing_clear(&gba->timing);
            gba->timing.globalCycles = clocks[c];
            gba->timing.masterCycles = (uint32_t)clocks[c];
            cupid_video_reset(&gba->video);
            cupid_io_write(gba, REG_DISPCNT, 0x0403);
            for (unsigned x = 0; x < 240 * 160; x++) gba->video.vram[x] = (uint16_t)(x & 0x7FFF);
            const uint32_t addresses[] = {GBA_BASE_IO + REG_GREENSWAP, GBA_BASE_IO + REG_BG2PA,
                GBA_BASE_VRAM + (3 * 240 + 8) * 2, GBA_BASE_IO + REG_GREENSWAP};
            const uint16_t values[] = {1, 0x0200, 0x03E0, 0};
            const unsigned times[] = {3 * 1232 + 60, 3 * 1232 + 77, 3 * 1232 + 89, 4 * 1232 + 110};
            for (unsigned j = 0; j < 4; j++) {
                writes[i][j] = (struct RasterWrite){gba, addresses[j], values[j]};
                events[i][j] = (struct CupidTimingEvent){.context = &writes[i][j], .callback = raster_write};
                cupid_timing_schedule(&gba->timing, &events[i][j], (int32_t)times[j]);
            }
        }
        cupid_timing_tick(&machines[0]->timing, 7000);
        for (unsigned i = 0; i < 7000; i++) cupid_timing_tick(&machines[1]->timing, 1);
        CHECK(!memcmp(machines[0]->video.outputBuffer, machines[1]->video.outputBuffer, sizeof(machines[0]->video.outputBuffer)));
        CHECK(!memcmp(machines[0]->video.vram, machines[1]->video.vram, GBA_SIZE_VRAM));
        CHECK(machines[0]->video.bg[2].sy == machines[1]->video.bg[2].sy);
        CHECK(machines[0]->video.event.when == machines[1]->video.event.when);
        destroy(machines[0]);
        destroy(machines[1]);
    }
}

static void tile_modes_fetch_text_and_affine_layers(void) {
    for (unsigned mode = 0; mode < 3; mode++) {
        struct CupidGBA* gba = bitmap(mode);
        gba->video.palette[0] = 0x7C00;
        gba->video.palette[1] = 0x001F;
        gba->video.palette[2] = 0x03E0;
        gba->video.palette[3] = 0x7FFF;
        for (unsigned i = 0; i < 16; i++) gba->video.vram[i] = 0x1111;
        for (unsigned i = 0; i < 32; i++) {
            gba->video.vram[0x4000 / 2 + i] = 0x0202;
            gba->video.vram[0x8000 / 2 + i] = 0x0303;
        }
        cupid_io_write(gba, REG_BG0CNT, 0x0301);
        cupid_io_write(gba, REG_BG2CNT, 0x2404);
        cupid_io_write(gba, REG_BG3CNT, 0x2509);
        cupid_io_write(gba, REG_BG3PD, 0);
        cupid_io_write(gba, REG_DISPCNT, (uint16_t)((mode == 0 ? 0x0100 : 0x0D00) | mode));
        cupid_timing_tick(&gba->timing, 3 * 1232 + 100);
        unsigned expected = mode == 0 ? 0x001F : 0x03E0;
        CHECK(gba->video.outputBuffer[3 * 240] == color16_to_rgba((uint16_t)expected));
        if (mode == 2) {
            cupid_io_write(gba, REG_DISPCNT, 0x0802);
            cupid_timing_tick(&gba->timing, 907);
            CHECK(gba->video.outputBuffer[3 * 240] == color16_to_rgba(0x03E0));
            CHECK(gba->video.outputBuffer[3 * 240 + 24] == color16_to_rgba(0x7FFF));
        }
        destroy(gba);
    }
}

static void bitmap_modes_select_frames_and_clip_coordinates(void) {
    for (unsigned mode = 3; mode <= 5; mode++) {
        for (unsigned frame = 0; frame < 2; frame++) {
            struct CupidGBA* gba = bitmap(mode);
            gba->video.palette[0] = 0x7C00;
            gba->video.palette[1] = 0x001F;
            gba->video.palette[2] = 0x03E0;
            for (unsigned i = 0; i < 240; i++) {
                gba->video.vram[i] = mode == 4 ? 0x0101 : 0x001F;
                gba->video.vram[0xA000 / 2 + i] = mode == 4 ? 0x0202 : 0x03E0;
            }
            cupid_io_write(gba, REG_DISPCNT, (uint16_t)(0x0400 | mode | (frame << 4)));
            cupid_timing_tick(&gba->timing, 3 * 1232 + 1007);
            uint32_t* row = gba->video.outputBuffer + 3 * 240;
            unsigned color = mode != 3 && frame ? 0x03E0 : 0x001F;
            CHECK(row[0] == color16_to_rgba((uint16_t)color));
            CHECK(row[159] == color16_to_rgba((uint16_t)color));
            CHECK(row[160] == color16_to_rgba((uint16_t)(mode == 5 ? 0x7C00 : color)));
            cupid_io_write(gba, REG_BG2X_LO, 0xFF00);
            cupid_io_write(gba, REG_BG2X_HI, 0xFFFF);
            cupid_timing_tick(&gba->timing, 1232);
            CHECK(row[240] == color16_to_rgba(0x7C00));
            CHECK(row[241] == color16_to_rgba((uint16_t)color));
            destroy(gba);
        }
    }
}

static void text_fetches_keep_scroll_flip_and_palette_bank(void) {
    struct CupidGBA* gba = bitmap(0);
    for (unsigned row = 0; row < 8; row++) {
        gba->video.vram[row * 2] = 0x4321;
        gba->video.vram[row * 2 + 1] = 0x8765;
    }
    for (unsigned i = 1; i <= 8; i++) gba->video.palette[i] = gba->video.palette[32 + i] = (uint16_t)i;
    gba->video.vram[0x1800 / 2] = 0x2400;
    cupid_io_write(gba, REG_BG0CNT, 0x0300);
    cupid_io_write(gba, REG_BG0HOFS, 3);
    cupid_io_write(gba, REG_DISPCNT, 0x0100);
    cupid_timing_tick(&gba->timing, 3 * 1232 + 1007);
    const uint16_t expected[] = {5, 4, 3, 2, 1, 1};
    for (unsigned x = 0; x < 6; x++) CHECK(gba->video.outputBuffer[3 * 240 + x] == color16_to_rgba(expected[x]));
    destroy(gba);
}

static void horizontal_windows_and_background_mosaic_are_applied_at_merge(void) {
    struct CupidGBA* gba = bitmap(3);
    for (unsigned x = 0; x < 240; x++) gba->video.vram[x] = (x + 1) & 31;
    gba->video.palette[0] = 0x7C00;
    cupid_io_write(gba, REG_BG2CNT, 0x0040);
    cupid_io_write(gba, REG_MOSAIC, 1);
    cupid_io_write(gba, REG_WIN0H, 0x1020);
    cupid_io_write(gba, REG_WIN0V, 0x01A0);
    cupid_io_write(gba, REG_WIN1H, 0xE608);
    cupid_io_write(gba, REG_WIN1V, 0x01A0);
    cupid_io_write(gba, REG_WININ, 0x0400);
    cupid_io_write(gba, REG_WINOUT, 4);
    cupid_io_write(gba, REG_DISPCNT, 0x6403);
    cupid_timing_tick(&gba->timing, 3 * 1232 + 1007);
    uint32_t* row = gba->video.outputBuffer + 3 * 240;
    CHECK(row[0] == color16_to_rgba(1) && row[1] == row[0]);
    CHECK(row[2] == color16_to_rgba(3) && row[3] == row[2]);
    CHECK(row[16] == color16_to_rgba(0x7C00) && row[31] == row[16]);
    CHECK(row[32] == color16_to_rgba(1));
    destroy(gba);
}

static void bitmap_vram_mirrors_reserve_the_background_bank(void) {
    for (unsigned mode = 0; mode <= 5; mode++) {
        struct CupidGBA* gba = bitmap(mode);
        cupid_memory_store32(&gba->cpu, GBA_BASE_VRAM + 0x10000, (int32_t)0xABAD1DEA, NULL);
        cupid_memory_store32(&gba->cpu, GBA_BASE_VRAM + 0x18000, (int32_t)0xDEADBEEF, NULL);
        uint32_t expected = mode >= 3 ? 0xABAD1DEA : 0xDEADBEEF;
        CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_VRAM + 0x10000, NULL) == expected);
        CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_VRAM + 0x18000, NULL) == (mode >= 3 ? 0 : expected));
        cupid_memory_store16(&gba->cpu, GBA_BASE_VRAM + 0x1C000, 0x1234, NULL);
        CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_VRAM + 0x14000, NULL) == 0x1234);
        if (mode >= 3) {
            CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_VRAM + 0x18000, NULL) == 0);
            CHECK(cupid_memory_load8(&gba->cpu, GBA_BASE_VRAM + 0x18001, NULL) == 0);
            cupid_memory_store16(&gba->cpu, GBA_BASE_VRAM + 0x18000, 0x5678, NULL);
            CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_VRAM + 0x10000, NULL) == expected);
            cupid_memory_set_active_region(&gba->cpu, GBA_BASE_VRAM + 0x18000);
            cupid_bus_fetch(&gba->cpu, GBA_BASE_VRAM + 0x18000, 4, false);
            uint32_t opcode;
            LOAD_32(opcode, 0, gba->cpu.memory.activeRegion);
            CHECK(opcode == 0);
            cupid_bus_release(&gba->cpu);
            cupid_bus_fetch(&gba->cpu, GBA_BASE_VRAM + 0x1C000, 4, true);
            LOAD_32(opcode, (GBA_BASE_VRAM + 0x1C000) & gba->cpu.memory.activeMask, gba->cpu.memory.activeRegion);
            CHECK((opcode & 0xFFFF) == 0x1234);
            cupid_bus_release(&gba->cpu);
        }
        destroy(gba);
    }
}

static void forced_blank_keeps_emitted_pixels_and_delays_release(void) {
    struct CupidGBA* gba = bitmap(3);
    for (unsigned x = 0; x < 240; x++) gba->video.vram[x] = 0x001F;
    cupid_timing_tick(&gba->timing, 3 * 1232 + 100);
    cupid_io_write(gba, REG_DISPCNT, 0x0483);
    cupid_timing_tick(&gba->timing, 907);
    uint32_t* row = gba->video.outputBuffer + 3 * 240;
    CHECK(row[0] == color16_to_rgba(0x001F));
    CHECK(row[14] == color16_to_rgba(0x7FFF) && row[239] == row[14]);
    cupid_timing_tick(&gba->timing, 3 * 1232 + 100 - 1007);
    cupid_io_write(gba, REG_DISPCNT, 0x0403);
    cupid_timing_tick(&gba->timing, 2 * 1232 + 1007 - 100);
    CHECK(gba->video.outputBuffer[8 * 240 + 239] == color16_to_rgba(0x7FFF));
    cupid_timing_tick(&gba->timing, 1232);
    CHECK(gba->video.outputBuffer[9 * 240 + 239] == color16_to_rgba(0x001F));
    destroy(gba);
}

static void affine_sprites_fetch_matrices_and_object_windows_mask_layers(void) {
    struct CupidGBA* gba = sprite_scene(BLEND_NONE, false, false, false, false);
    gba->video.oam.obj[0].a = 0x2300; /* 8bpp, affine, double-size bounding box. */
    gba->video.oam.mat[0].a = gba->video.oam.mat[0].d = 0x0100;
    gba->video.oam.mat[0].b = gba->video.oam.mat[0].c = 0;
    for (unsigned y = 0; y < 8; y++) {
        for (unsigned x = 0; x < 8; x++) ((uint8_t*)gba->video.vram)[0x14000 + y * 8 + x] = (uint8_t)(x + 1);
    }
    for (unsigned i = 1; i <= 8; i++) gba->video.palette[256 + i] = (uint16_t)i;
    cupid_timing_tick(&gba->timing, 5 * 1232 + 1007);
    uint32_t* row = gba->video.outputBuffer + 5 * 240;
    CHECK(row[0] == color16_to_rgba(0x4000));
    for (unsigned x = 0; x < 8; x++) CHECK(row[4 + x] == color16_to_rgba((uint16_t)(x + 1)));
    CHECK(row[12] == color16_to_rgba(0x4000));
    destroy(gba);

    gba = sprite_scene(BLEND_NONE, false, false, false, false);
    gba->video.oam.obj[0].a = 0x0800;
    gba->video.palette[0] = 0x03E0;
    cupid_io_write(gba, REG_WINOUT, 0x0400);
    cupid_io_write(gba, REG_DISPCNT, 0x9443);
    cupid_timing_tick(&gba->timing, 3 * 1232 + 1007);
    row = gba->video.outputBuffer + 3 * 240;
    CHECK(row[0] == color16_to_rgba(0x4000) && row[7] == row[0]);
    CHECK(row[8] == color16_to_rgba(0x03E0));
    destroy(gba);
}

int main(void) {
    RUN(green_swap_exchanges_only_adjacent_green_components);
    RUN(register_changes_affect_only_later_scanout);
    RUN(fetches_and_palette_reads_retain_their_values);
    RUN(affine_steps_and_reference_writes_keep_separate_latches);
    RUN(effects_use_the_two_visible_layers_and_window_control);
    RUN(display_memory_beats_wait_for_the_fetch_unit);
    RUN(dma_word_writes_publish_each_display_bus_beat);
    RUN(object_attribute_fetches_are_retained_for_the_next_line);
    RUN(late_dispatch_preserves_pixels_across_clock_wrap);
    RUN(tile_modes_fetch_text_and_affine_layers);
    RUN(bitmap_modes_select_frames_and_clip_coordinates);
    RUN(text_fetches_keep_scroll_flip_and_palette_bank);
    RUN(horizontal_windows_and_background_mosaic_are_applied_at_merge);
    RUN(bitmap_vram_mirrors_reserve_the_background_bank);
    RUN(forced_blank_keeps_emitted_pixels_and_delays_release);
    RUN(affine_sprites_fetch_matrices_and_object_windows_mask_layers);
    return 0;
}
