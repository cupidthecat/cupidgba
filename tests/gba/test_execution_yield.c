/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_execution_yield.c
 * @brief Execution-boundary yields and retained device event state.
 */
#include "../test.h"
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"

enum { START = GBA_BASE_IWRAM + 0x1000 };

struct Capture {
    struct CupidGBA *gba;
    unsigned frames, resumed, issues;
    uint64_t clocks[4];
    uint32_t counters[4];
    bool suspend, input;
};

static void input(struct Capture *capture) {
    capture->gba->keysActive = (uint16_t)capture->frames;
    cupid_gba_test_keypad_irq(capture->gba);
}

static void present(void *context, const uint32_t *pixels, uint64_t clock) {
    struct Capture *capture = context;
    CHECK(pixels == capture->gba->video.outputBuffer && capture->frames < 4);
    capture->clocks[capture->frames] = clock;
    capture->counters[capture->frames++] = capture->gba->video.frameCounter;
    if (capture->suspend) {
        CHECK(cupid_gba_yield_steps(capture->gba));
        ++capture->resumed;
    } else if (capture->input) {
        input(capture);
    }
}

static void issued(const struct CupidARM *cpu, void *context) {
    struct Capture *capture = context;
    (void)cpu;
    ++capture->issues;
}

static struct CupidGBA *machine(unsigned thumb, unsigned wrap, struct Capture *capture) {
    struct CupidGBA *gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    const uint64_t epoch = wrap ? UINT64_C(0x2FFFFFFFE) : 0;
    gba->timing.globalCycles = epoch;
    gba->timing.masterCycles = (uint32_t)epoch;
    cupid_video_reset(&gba->video);
    cupid_audio_reset(&gba->audio);
    gba->audio.sampleOrigin = gba->audio.lastSample = epoch;
    gba->audio.nextSample = epoch + 512;
    cupid_irq_init(gba);
    /* Pre-roll puts two complete VBlank edges inside the upcoming DMA wait. */
    cupid_timing_tick(&gba->timing, 100000);
    gba->video.frameCounter = wrap ? UINT32_MAX - 1 : 0;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    cupid_io_write(gba, REG_KEYCNT, 0x4001);
    cupid_io_write(gba, REG_IE, IRQ_KEYPAD);
    const uint32_t arm[] = {0xE1A00000, 0xE2877001, 0xEAFFFFFE};
    const uint16_t narrow[] = {0x46C0, 0x3701, 0xE7FE};
    memcpy(gba->memory.iwram + 0x1000, thumb ? (const void *)narrow : (const void *)arm,
           thumb ? sizeof(narrow) : sizeof(arm));
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = START;
    if (thumb)
        CupidThumbWritePC(&gba->cpu);
    else
        CupidARMWritePC(&gba->cpu);
    gba->cpu.gprs[7] = 0;
    const uint32_t pattern = UINT32_C(0x19283746);
    memcpy(gba->memory.iwram, &pattern, sizeof(pattern));
    cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM);
    cupid_dma_write_dad(gba, 3, GBA_BASE_EWRAM);
    cupid_dma_write_cnt_lo(gba, 3, 0);
    cupid_dma_write_cnt(gba, 3, 0x8500);
    cupid_timing_tick(&gba->timing, 3);
    CHECK(gba->memory.dmaRunning);
    capture->gba = gba;
    cupid_video_set_frame_callback(&gba->video, present, capture);
    gba->cpu.instructionObserver = issued;
    gba->cpu.instructionObserverContext = capture;
    return gba;
}

static void destroy(struct CupidGBA *gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void same_queue(const struct CupidTimingEvent *a, const struct CupidTimingEvent *b) {
    while (a && b) {
        CHECK(a->when == b->when && a->priority == b->priority && a->callback == b->callback);
        CHECK((a->name == NULL) == (b->name == NULL));
        if (a->name)
            CHECK(!strcmp(a->name, b->name));
        a = a->next;
        b = b->next;
    }
    CHECK(!a && !b);
}

static void same(const struct CupidGBA *a, const struct CupidGBA *b) {
    CHECK(cupid_timing_global_time(&a->timing) == cupid_timing_global_time(&b->timing));
    CHECK(a->cpu.cycles == b->cpu.cycles && a->cpu.nextEvent == b->cpu.nextEvent);
    CHECK(!memcmp(a->cpu.gprs, b->cpu.gprs, sizeof(a->cpu.gprs)));
    CHECK(!memcmp(a->cpu.prefetch, b->cpu.prefetch, sizeof(a->cpu.prefetch)));
    CHECK(a->cpu.cpsr.packed == b->cpu.cpsr.packed && a->cpu.spsr.packed == b->cpu.spsr.packed);
    CHECK(a->cpuInstructionPending == b->cpuInstructionPending && a->bus == b->bus);
    CHECK(a->cpu.memory.timed == b->cpu.memory.timed && a->cpuBlocked == b->cpuBlocked);
    CHECK(a->timing.waitingForEvent == b->timing.waitingForEvent);
    CHECK(a->timing.inCallback == b->timing.inCallback);
    CHECK(a->keysActive == b->keysActive && a->irqf == b->irqf);
    CHECK(a->irq.available == b->irq.available && a->irq.line == b->irq.line);
    CHECK(!memcmp(a->memory.ewram, b->memory.ewram, GBA_SIZE_EWRAM));
    CHECK(!memcmp(a->memory.iwram, b->memory.iwram, GBA_SIZE_IWRAM));
    CHECK(!memcmp(a->memory.io, b->memory.io, sizeof(a->memory.io)));
    for (unsigned i = 0; i < GBA_DMA_COUNT; ++i) {
        struct CupidDMA x, y;
        memcpy(&x, &a->memory.dma[i], sizeof(x));
        memcpy(&y, &b->memory.dma[i], sizeof(y));
        /* Instance pointers differ; every transfer field and event value must match. */
        x.gba = y.gba = NULL;
        x.requestEvent.context = y.requestEvent.context = NULL;
        x.completionEvent.context = y.completionEvent.context = NULL;
        x.requestEvent.next = y.requestEvent.next = NULL;
        x.completionEvent.next = y.completionEvent.next = NULL;
        CHECK(!memcmp(&x, &y, sizeof(x)));
    }
    CHECK(a->memory.dmaRunning == b->memory.dmaRunning);
    CHECK(!memcmp(a->video.outputBuffer, b->video.outputBuffer, sizeof(a->video.outputBuffer)));
    CHECK(a->video.frameCounter == b->video.frameCounter && a->frameReady == b->frameReady);
    CHECK(a->audio.sampleWritePos == b->audio.sampleWritePos);
    same_queue(a->timing.root, b->timing.root);
    same_queue(a->timing.reroot, b->timing.reroot);
}

static bool reject_boundary_yield(struct CupidGBA *gba, uint64_t steps, void *context) {
    (void)steps;
    (void)context;
    CHECK(!cupid_gba_yield_steps(gba));
    return true;
}

static void paired(void) {
    unsigned scenes = 0;
    for (unsigned thumb = 0; thumb < 2; ++thumb)
        for (unsigned wrap = 0; wrap < 2; ++wrap)
            for (unsigned keys = 0; keys < 2; ++keys) {
                struct Capture captures[2] = {{.input = keys}, {.suspend = true, .input = keys}};
                struct CupidGBA *cores[] = {machine(thumb, wrap, &captures[0]),
                                            machine(thumb, wrap, &captures[1])};
                CHECK(!cupid_gba_yield_steps(cores[1]));
                CHECK(cupid_gba_run_steps(cores[0], 1, NULL, NULL) == 1);
                for (unsigned frame = 1; frame <= 2; ++frame) {
                    CHECK(cupid_gba_run_steps(cores[1], 1, reject_boundary_yield, NULL) == 0);
                    CHECK(captures[1].frames == frame && captures[1].resumed == frame - 1);
                    CHECK(cores[1]->cpuInstructionPending && cores[1]->memory.dmaRunning);
                    CHECK(cores[1]->video.vcount == VIDEO_VDRAW_LINES);
                    CHECK(cupid_timing_global_time(&cores[1]->timing) ==
                          captures[1].clocks[frame - 1]);
                    CHECK(!cupid_gba_yield_steps(cores[1]));
                    if (keys)
                        input(&captures[1]);
                }
                CHECK(cupid_gba_run_steps(cores[1], 1, reject_boundary_yield, NULL) == 1);
                CHECK(captures[0].frames == 2 && captures[1].resumed == 2);
                CHECK(captures[0].issues == 1 && captures[1].issues == 1);
                CHECK(!memcmp(captures[0].clocks, captures[1].clocks, sizeof(captures[0].clocks)));
                CHECK(!memcmp(captures[0].counters, captures[1].counters,
                              sizeof(captures[0].counters)));
                CHECK(captures[1].counters[1] == (wrap ? 0 : 2));
                same(cores[0], cores[1]);
                /* Resuming finishes the same fetch once, then executes the next opcode once. */
                for (unsigned i = 0; i < 2; ++i) {
                    CHECK(!cores[i]->cpuInstructionPending && !cores[i]->memory.dmaRunning);
                    CHECK(cores[i]->cpu.gprs[7] == 0);
                    cupid_gba_step(cores[i]);
                    CHECK(cores[i]->cpu.gprs[7] == 1 && captures[i].issues == 2);
                }
                same(cores[0], cores[1]);
                for (unsigned i = 0; i < 2; ++i)
                    destroy(cores[i]);
                ++scenes;
            }
    printf(
        "PASS %u suspended/uninterrupted DMA pairs, frame/clock wrap, input and exact resumption\n",
        scenes);
}

static void rejected(void *context, const uint32_t *pixels, uint64_t clock) {
    struct Capture *capture = context;
    (void)pixels;
    (void)clock;
    ++capture->frames;
    CHECK(!cupid_gba_yield_steps(capture->gba));
}

static void restrictions(void) {
    for (unsigned mode = 0; mode < 4; ++mode) {
        struct Capture capture = {0};
        struct CupidGBA *gba = machine(0, 0, &capture);
        cupid_video_set_frame_callback(&gba->video, rejected, &capture);
        if (mode == 0)
            cupid_gba_step(gba);
        else if (mode == 1)
            cupid_gba_run_frame(gba);
        else if (mode == 2)
            cupid_gba_run_interval(gba, 600000);
        else {
            gba->externalExecution = true;
            CHECK(cupid_gba_run_steps(gba, 1, NULL, NULL) == 1);
        }
        CHECK(capture.frames >= 2 && !gba->cpuInstructionPending);
        if (mode == 2)
            CHECK(gba->intervalOvershoot < 100);
        destroy(gba);
    }
    puts("PASS non-batched and external execution reject callback suspension");
}

static void resume_requests(void) {
    for (unsigned thumb = 0; thumb < 2; ++thumb)
        for (unsigned wrap = 0; wrap < 2; ++wrap)
            for (unsigned request = 0; request < 3; ++request) {
                struct Capture captures[2] = {{0}, {.suspend = true}};
                struct CupidGBA *cores[] = {machine(thumb, wrap, &captures[0]),
                                            machine(thumb, wrap, &captures[1])};
                uint64_t start = cupid_timing_global_time(&cores[0]->timing);
                CHECK(cupid_gba_run_steps(cores[1], 1, NULL, NULL) == 0);
                CHECK(captures[1].frames == 1 && captures[1].resumed == 0);
                captures[1].suspend = false;
                uint64_t elapsed = cupid_timing_global_time(&cores[1]->timing) - start;
                for (unsigned i = 0; i < 2; ++i) {
                    if (request == 0)
                        cupid_gba_step(cores[i]);
                    else if (request == 1)
                        cupid_gba_run_frame(cores[i]);
                    else
                        cupid_gba_run_interval(cores[i], 400000 + (i == 0 ? (uint32_t)elapsed : 0));
                }
                CHECK(captures[1].resumed == 1 && captures[0].frames == captures[1].frames);
                CHECK(!memcmp(captures[0].clocks, captures[1].clocks, sizeof(captures[0].clocks)));
                CHECK(captures[0].issues == captures[1].issues);
                CHECK(cores[0]->intervalOvershoot == cores[1]->intervalOvershoot);
                same(cores[0], cores[1]);
                for (unsigned i = 0; i < 2; ++i)
                    destroy(cores[i]);
            }
    puts("PASS 12 pending-callback transitions to step, frame and paced interval requests");
}

static void lifetime(void) {
    for (unsigned reset = 0; reset < 2; ++reset) {
        struct Capture capture = {.suspend = true};
        struct CupidGBA *gba = machine(0, 0, &capture);
        CHECK(cupid_gba_run_steps(gba, 1, NULL, NULL) == 0);
        CHECK(capture.frames == 1 && capture.resumed == 0 && gba->cpuInstructionPending);
        if (reset) {
            cupid_gba_reset(gba);
            CHECK(!gba->execution && !gba->cpuInstructionPending && !gba->timing.inCallback);
            CHECK(!gba->memory.dmaRunning && !gba->irq.cpuClockPaused && !gba->cpu.memory.timed);
            CHECK(!cupid_gba_yield_steps(gba));
            CHECK(capture.frames == 1 && capture.resumed == 0);
        }
        destroy(gba);
        CHECK(capture.frames == 1 && capture.resumed == 0);
    }
    puts("PASS reset/destroy cancel a suspended peripheral callback without resuming it");
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "paired"))
        paired();
    else if (!strcmp(argv[1], "restrictions")) {
        restrictions();
        resume_requests();
    } else if (!strcmp(argv[1], "lifetime"))
        lifetime();
    else
        return 2;
    return 0;
}
