/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file probe_adapter.c
 * @brief Probe lifecycle adapter and checked runner boundaries.
 */
#include "../support/probe_run.h"
#include <errno.h>
#include <inttypes.h>

static uint64_t ownedFrameLimit;

/* Compile the real probe entry points with a tighter limit for owned inputs.
 * Original cartridge launchers and their production limits are unchanged. */
static struct CupidTestProbeRun owned_begin(const struct CupidGBA *gba, uint32_t terminal,
                                            uint64_t frames, uint64_t clockLimit) {
    if (frames > ownedFrameLimit)
        frames = ownedFrameLimit;
    return cupid_test_probe_begin(gba, terminal, frames, clockLimit);
}

static bool owned_boundary(struct CupidGBA *gba, uint64_t steps, void *context) {
    bool more = cupid_test_probe_boundary(gba, steps, context);
    if (!more) {
        const struct CupidTestProbeRun *run = context;
        printf("owned-boundary issued=%u completed=%u frames=%" PRIu64 "\n",
               (unsigned)run->terminalIssued, (unsigned)run->terminalCompleted, run->frames);
    }
    return more;
}

#define cupid_test_probe_boundary owned_boundary
#define cupid_test_probe_begin owned_begin
#define main probe_original_main
#if CUPID_TEST_PROBE_ADAPTER == 1
#include "../arm/irq_probe.c"
#define probe_entry cupid_test_irq_source_probe
#elif CUPID_TEST_PROBE_ADAPTER == 2
#include "../arm/saved_status_probe.c"
#define probe_entry probe_original_main
#elif CUPID_TEST_PROBE_ADAPTER == 3
#include "../arm/user_bank_probe.c"
#define probe_entry cupid_test_user_bank_probe
#elif CUPID_TEST_PROBE_ADAPTER == 4
#include "../roms/cpu_corpus_runner.c"
#define probe_entry probe_original_main
#else
#error Unknown owned probe adapter
#endif
#undef main
#undef cupid_test_probe_begin
#undef cupid_test_probe_boundary

int main(int argc, char **argv) {
    if (argc < 3)
        return 2;
    char *end;
    errno = 0;
    ownedFrameLimit = strtoull(argv[1], &end, 0);
    if (errno || !argv[1][0] || *end || !ownedFrameLimit || ownedFrameLimit > 64)
        return 2;
    return probe_entry(argc - 1, argv + 1);
}
