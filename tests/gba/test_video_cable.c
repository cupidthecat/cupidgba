/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/cable.h"
#include "cupid/isa-inlines.h"
#include "../test.h"
#include <inttypes.h>

struct Paint { struct CupidGBA* gba; uint16_t color; };
static void paint(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing; (void)late;
    const struct Paint* p = context;
    /* This halfword has not been fetched when the write arrives. */
    cupid_memory_store16(&p->gba->cpu, GBA_BASE_VRAM + 8, (int16_t)p->color, NULL);
}

static struct CupidGBA* machine(uint64_t epoch, uint16_t color, bool active) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba)); CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = epoch;
    gba->timing.masterCycles = (uint32_t)epoch;
    gba->cpu.cpsr.i = gba->cpu.cpsr.f = 1;
    gba->cpu.irqDisableLatch = true;
    cupid_video_reset(&gba->video);
    gba->video.vcount = 225;
    gba->memory.io[REG_VCOUNT >> 1] = 225;
    cupid_io_write(gba, REG_DISPCNT, 0x0403);
    for (unsigned i = 0; i < GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT; ++i)
        gba->video.vram[i] = color;
    /* Establish the display latches in VBlank before observing visible pixels. */
    cupid_timing_tick(&gba->timing, 3 * VIDEO_HORIZONTAL_LENGTH);
    CHECK(gba->video.vcount == 0 && gba->cpu.cycles == 0);
    if (active) {
        const uint32_t code[] = {0xEAFFFFFE, 0xE1A00000};
        memcpy(gba->memory.ewram, code, sizeof(code));
        CHECK(!gba->cpu.memory.timed);
        gba->cpu.gprs[ARM_PC] = GBA_BASE_EWRAM;
        _CupidSetMode(&gba->cpu, MODE_ARM);
        CupidARMWritePC(&gba->cpu);
    }
    gba->cpu.halted = !active;
    return gba;
}

static void same_display(const struct CupidGBA* actual, const struct CupidGBA* expected,
                         unsigned player, uint32_t endpoint) {
    CHECK(cupid_timing_global_time(&actual->timing) == cupid_timing_global_time(&expected->timing));
    CHECK(actual->video.vcount == expected->video.vcount);
    CHECK(actual->video.dispstat == expected->video.dispstat);
    CHECK(actual->video.frameCounter == expected->video.frameCounter);
    for (unsigned i = 0; i < GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT; ++i) {
        if (actual->video.outputBuffer[i] != expected->video.outputBuffer[i]) {
            printf("player=%u endpoint=%u pixel=%u actual=%08x expected=%08x\n",
                player, endpoint, i, actual->video.outputBuffer[i], expected->video.outputBuffer[i]);
            CHECK(actual->video.outputBuffer[i] == expected->video.outputBuffer[i]);
        }
    }
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    bool active = !strcmp(argv[1], "active");
    CHECK(active || !strcmp(argv[1], "halt"));
    const uint64_t epochs[] = {0, INT32_MAX - UINT64_C(8192),
        UINT32_MAX - UINT64_C(8192), UINT64_C(0x300000000)};
    const uint32_t endpoints[] = {1, 2, 35, 39, 47, 49, 53, 61, 63, 1006, 1007,
        1232, 1233, 4 * VIDEO_HORIZONTAL_LENGTH + 61,
        VIDEO_VDRAW_LINES * VIDEO_HORIZONTAL_LENGTH, VIDEO_TOTAL_LENGTH + 53};
    const uint16_t colors[] = {0x001F, 0x03E0, 0x7C00, 0x03FF};
    unsigned snapshots = 0;
    for (unsigned epoch = 0; epoch < 4; ++epoch) for (unsigned count = 2; count <= 4; count += 2) {
        struct CupidGBA* cores[4] = {0}, *reference[4] = {0};
        struct Paint writes[4][2];
        struct CupidTimingEvent events[4][2];
        for (unsigned i = 0; i < count; ++i) {
            const uint64_t clock = epochs[epoch] + i * 31;
            cores[i] = machine(clock, colors[i], active);
            reference[i] = machine(clock, colors[i], active);
            if (count == 4 && i == 3) cores[i]->stopped = reference[i]->stopped = true;
            struct CupidGBA* pair[] = {cores[i], reference[i]};
            for (unsigned side = 0; side < 2; ++side) {
                writes[i][side] = (struct Paint){pair[side], (uint16_t)(colors[i] ^ 0x7FFF)};
                events[i][side] = (struct CupidTimingEvent){.context = &writes[i][side],
                    .callback = paint, .priority = 0, .name = "Display-memory write during CPU wait"};
                cupid_timing_schedule(&pair[side]->timing, &events[i][side], 47);
            }
        }
        struct CupidCable* cable = cupid_cable_create(cores, count); CHECK(cable);
        uint32_t previous = 0;
        for (unsigned e = 0; e < sizeof(endpoints) / sizeof(*endpoints); ++e) {
            uint32_t delta = endpoints[e] - previous;
            CHECK(cupid_cable_run(cable, delta));
            CHECK(cupid_cable_time(cable) == endpoints[e]);
            for (unsigned i = 0; i < count; ++i) {
                if (!reference[i]->stopped) cupid_timing_tick(&reference[i]->timing, (int32_t)delta);
                same_display(cores[i], reference[i], i, endpoints[e]);
                ++snapshots;
            }
            previous = endpoints[e];
        }
        cupid_cable_destroy(cable);
        for (unsigned i = 0; i < count; ++i) {
            cupid_gba_destroy(cores[i]); free(cores[i]);
            cupid_gba_destroy(reference[i]); free(reference[i]);
        }
    }
    CHECK(snapshots == 384);
    printf("%s: %u complete display snapshots; 14745600 compared pixels\n", argv[1], snapshots);
    return 0;
}
