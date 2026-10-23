/* SPDX-License-Identifier: GPL-3.0-or-later */
#define SDL_MAIN_HANDLED
#include "cupid/platform.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned polls, intervals, presents, flushes, dirtyFlushes;
static bool stopped;

static bool observed_poll(struct CupidPlatform* platform, struct CupidGBA* gba) {
    const char* quit = getenv("CUPID_TEST_QUIT_AFTER");
    unsigned limit = quit ? (unsigned)strtoul(quit, NULL, 10) : 400;
    if (polls++ == limit) return false;
    return cupid_platform_poll_events(platform, gba);
}

static void observed_interval(struct CupidGBA* gba, uint32_t clocks) {
    if (clocks != VIDEO_TOTAL_LENGTH) exit(2);
    cupid_gba_run_interval(gba, clocks);
    ++intervals;
    stopped = gba->stopped;
}

static void observed_present(struct CupidPlatform* platform, const uint32_t* image) {
    ++presents;
    cupid_platform_render_frame(platform, image);
}

static bool observed_flush(struct CupidSavedata* save, const char* path) {
    printf("FLUSH intervals=%u dirty=%u\n", intervals, (unsigned)save->dirty);
    ++flushes;
    if (save->dirty) ++dirtyFlushes;
    return cupid_savedata_save_file(save, path);
}

/* Remove host pacing only. The included frontend still runs real input,
 * intervals, rendering, audio, battery loading and shutdown implementations. */
static uint64_t test_frequency(void) { return UINT64_C(1000000); }
static uint64_t test_counter(void) {
    static uint64_t value;
    value += UINT64_C(1000000);
    return value;
}

#define main frontend_main
#define cupid_platform_poll_events observed_poll
#define cupid_gba_run_interval observed_interval
#define cupid_platform_render_frame observed_present
#define cupid_savedata_save_file observed_flush
#define SDL_GetPerformanceFrequency test_frequency
#define SDL_GetPerformanceCounter test_counter
#include "../../src/platform/main.c"
#undef SDL_GetPerformanceCounter
#undef SDL_GetPerformanceFrequency
#undef cupid_savedata_save_file
#undef cupid_platform_render_frame
#undef cupid_gba_run_interval
#undef cupid_platform_poll_events
#undef main

int main(int argc, char** argv) {
    int result = frontend_main(argc, argv);
    printf("OBS polls=%u intervals=%u presents=%u flushes=%u dirty=%u stopped=%u\n",
           polls, intervals, presents, flushes, dirtyFlushes, (unsigned)stopped);
    return result;
}
