/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/cable.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

enum { START = GBA_BASE_IWRAM + 0x1000, TARGET = START + 0x40 };
struct Observations { unsigned branch, add, store; };

static void observe(const struct CupidARM* cpu, void* argument) {
    struct Observations* seen = argument;
    const uint32_t pc = cupid_arm_instruction_address(cpu);
    const unsigned width = cpu->executionMode == MODE_THUMB ? 2 : 4;
    if (pc == START) ++seen->branch;
    if (pc == TARGET) ++seen->add;
    if (pc == TARGET + width) ++seen->store;
}

static struct CupidGBA* machine(unsigned thumb, unsigned wrap) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba)); CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    const uint64_t epoch = wrap ? UINT32_MAX - UINT64_C(3) : 0;
    gba->timing.globalCycles = epoch;
    gba->timing.masterCycles = (uint32_t)epoch;
    cupid_video_reset(&gba->video);
    cupid_audio_reset(&gba->audio);
    /* Console reset normally runs at time zero. Move this synthetic epoch's
     * audio divider with the scheduler, rather than replaying samples from
     * zero up to the wrap anchor at the first frame-sequencer callback. */
    gba->audio.sampleOrigin = gba->audio.lastSample = epoch;
    gba->audio.nextSample = epoch + 512;
    cupid_irq_init(gba);
    if (thumb) {
        const uint16_t branch[] = {0xE01E, 0x46C0}; /* b target; nop */
        const uint16_t target[] = {0x3701, 0x6037, 0xE7FE}; /* add; str; b . */
        memcpy(gba->memory.iwram + 0x1000, branch, sizeof(branch));
        memcpy(gba->memory.iwram + 0x1040, target, sizeof(target));
        _CupidSetMode(&gba->cpu, MODE_THUMB);
        gba->cpu.gprs[ARM_PC] = START;
        CupidThumbWritePC(&gba->cpu);
    } else {
        const uint32_t branch[] = {0xEA00000E, 0xE1A00000};
        const uint32_t target[] = {0xE2877001, 0xE5867000, 0xEAFFFFFE};
        memcpy(gba->memory.iwram + 0x1000, branch, sizeof(branch));
        memcpy(gba->memory.iwram + 0x1040, target, sizeof(target));
        gba->cpu.gprs[ARM_PC] = START;
        CupidARMWritePC(&gba->cpu);
    }
    gba->cpu.gprs[6] = GBA_BASE_EWRAM;
    gba->cpu.gprs[7] = 0;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    return gba;
}

static void request_halt(struct CupidGBA* gba) {
    /* An owned bus fixture supplies the BIOS-only port strobe. The following
     * branch must retain its first target fetch until an eligible wake. */
    gba->memory.activeRegion = GBA_REGION_BIOS;
    gba->cpu.memory.timed = true;
    cupid_io_write8(gba, REG_HALTCNT, 0);
    gba->cpu.memory.timed = false;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void frames(void) {
    for (unsigned wrap = 0; wrap < 2; ++wrap) for (unsigned thumb = 0; thumb < 2; ++thumb) {
        struct CupidGBA* gba = machine(thumb, wrap);
        struct Observations seen = {0};
        gba->cpu.instructionObserver = observe;
        gba->cpu.instructionObserverContext = &seen;
        request_halt(gba);
        for (unsigned frame = 1; frame <= 3; ++frame) {
            cupid_gba_run_frame(gba);
            CHECK(gba->frameReady && gba->video.frameCounter == frame);
            CHECK(gba->cpu.halted && gba->cpuInstructionPending && !gba->irq.available);
            CHECK(seen.branch == 1 && !seen.add && !seen.store);
            CHECK(gba->cpu.gprs[7] == 0 && gba->memory.ewram[0] == 0);
        }
        destroy(gba);
    }
}

static void interval(void) {
    const uint32_t requests[] = {1, 1, 0, 7, 1232, 3, 280896, 17};
    for (unsigned wrap = 0; wrap < 2; ++wrap) for (unsigned thumb = 0; thumb < 2; ++thumb) {
        struct CupidGBA* gba = machine(thumb, wrap);
        request_halt(gba);
        uint64_t charged = 0;
        const uint64_t start = cupid_timing_global_time(&gba->timing);
        for (unsigned i = 0; i < sizeof(requests) / sizeof(*requests); ++i) {
            cupid_gba_run_interval(gba, requests[i]);
            charged += requests[i];
            CHECK(cupid_timing_global_time(&gba->timing) - start == charged + gba->intervalOvershoot);
            CHECK(gba->cpu.halted && gba->cpuInstructionPending && !gba->irq.available);
            CHECK(gba->cpu.gprs[7] == 0);
        }
        destroy(gba);
    }
}

static void wake(void) {
    for (unsigned wrap = 0; wrap < 2; ++wrap) for (unsigned thumb = 0; thumb < 2; ++thumb) {
        struct CupidGBA* gba = machine(thumb, wrap);
        struct Observations seen = {0};
        gba->cpu.instructionObserver = observe;
        gba->cpu.instructionObserverContext = &seen;
        request_halt(gba);
        cupid_gba_run_frame(gba);
        CHECK(gba->cpu.halted && gba->cpuInstructionPending);
        cupid_io_write(gba, REG_IE, IRQ_KEYPAD);
        cupid_raise_irq(gba, IRQ_KEYPAD, 0);
        cupid_gba_run_interval(gba, 64);
        CHECK(!gba->cpu.halted && !gba->cpuInstructionPending);
        CHECK(seen.branch == 1 && seen.add == 1 && seen.store == 1);
        CHECK(gba->cpu.gprs[7] == 1 && gba->memory.ewram[0] == 1);
        CHECK(gba->cpu.privilegeMode == MODE_SYS && gba->cpu.executionMode == (thumb ? MODE_THUMB : MODE_ARM));
        destroy(gba);
    }
}

static void lifetime(void) {
    /* Two owners retain independent stacks on one thread. Cancel the first
     * while it holds a fetch; the second must still resume and wake normally. */
    struct CupidGBA* first = machine(0, 0);
    struct CupidGBA* second = machine(1, 1);
    request_halt(first); request_halt(second);
    cupid_gba_run_frame(first); cupid_gba_run_frame(second);
    CHECK(first->cpuInstructionPending && second->cpuInstructionPending);
    cupid_gba_reset(first);
    CHECK(!first->execution && !first->cpuInstructionPending && !first->cpu.halted);
    const uint32_t replacement[] = {0xE3A07009, 0xEAFFFFFE}; /* mov r7,#9; b . */
    memcpy(first->memory.iwram + 0x2000, replacement, sizeof(replacement));
    first->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x2000;
    CupidARMWritePC(&first->cpu);
    cupid_gba_run_interval(first, 64);
    CHECK(first->cpu.gprs[7] == 9 && first->memory.ewram[0] == 0);
    destroy(first);
    cupid_gba_run_frame(second);
    CHECK(second->video.frameCounter == 2 && second->cpu.halted);
    cupid_io_write(second, REG_IE, IRQ_KEYPAD);
    cupid_raise_irq(second, IRQ_KEYPAD, 0);
    cupid_gba_run_interval(second, 64);
    CHECK(second->cpu.gprs[7] == 1 && second->memory.ewram[0] == 1);
    destroy(second);
    /* Host conversion/reference ownership must also be reusable after the
     * last suspended stack has been destroyed. */
    first = machine(0, 0);
    cupid_gba_step(first);
    CHECK(!first->cpuInstructionPending);
    destroy(first);
}

static void ready_wake(void) {
    for (unsigned thumb = 0; thumb < 2; ++thumb) {
        struct CupidGBA* gba = machine(thumb, 0);
        struct Observations seen = {0};
        gba->cpu.instructionObserver = observe;
        gba->cpu.instructionObserverContext = &seen;
        cupid_io_write(gba, REG_IE, IRQ_KEYPAD);
        cupid_raise_irq(gba, IRQ_KEYPAD, 0);
        cupid_timing_tick(&gba->timing, 3);
        CHECK(gba->irq.available);
        request_halt(gba);
        cupid_gba_step(gba);
        CHECK(gba->cpu.halted && gba->cpuInstructionPending);
        CHECK(seen.branch == 1 && !seen.add && !seen.store);
        cupid_gba_run_interval(gba, 64);
        CHECK(!gba->cpu.halted && !gba->cpuInstructionPending);
        CHECK(seen.branch == 1 && seen.add == 1 && seen.store == 1);
        CHECK(gba->cpu.gprs[7] == 1 && gba->memory.ewram[0] == 1);
        destroy(gba);
    }
}

static void cable(void) {
    struct CupidGBA* cores[] = {machine(0, 0), machine(1, 0)};
    /* Previously run standalone cores retain idle stacks. A cable shares
     * their host and supplies its own continuation at every clock request. */
    cupid_gba_run_interval(cores[0], 1); cupid_gba_run_interval(cores[1], 1);
    cupid_gba_process_events(cores[0]); cupid_gba_process_events(cores[1]);
    CHECK(!cores[0]->cpuInstructionPending && !cores[1]->cpuInstructionPending);
    CHECK(!cores[0]->cpu.cycles && !cores[1]->cpu.cycles);
    request_halt(cores[0]); request_halt(cores[1]);
    struct CupidCable* link = cupid_cable_create(cores, 2); CHECK(link);
    CHECK(cupid_cable_run(link, 280896));
    CHECK(cores[0]->cpu.halted && cores[1]->cpu.halted);
    CHECK(cores[0]->video.frameCounter == 1 && cores[1]->video.frameCounter == 1);
    cupid_cable_destroy(link);
    for (unsigned i = 0; i < 2; ++i) {
        cupid_gba_reset(cores[i]);
        CHECK(!cores[i]->cpuInstructionPending);
        destroy(cores[i]);
    }
    /* An unfinished standalone instruction cannot be silently abandoned by
     * attaching a cable. Reset or complete that instruction first. */
    cores[0] = machine(0, 0); cores[1] = machine(0, 0);
    request_halt(cores[0]); cupid_gba_run_frame(cores[0]);
    CHECK(!cupid_cable_create(cores, 2));
    destroy(cores[0]); destroy(cores[1]);
}

struct BoundaryRecord {
    int32_t registers[16], cycles, nextEvent;
    uint32_t cpsr, spsr, prefetch[2], bus, frame, samples;
    uint64_t clock;
    uint16_t keys, ie, irqf;
    bool halted, pending, available, line, frameReady, waiting, timed;
};
struct Boundaries {
    struct BoundaryRecord* records;
    uint64_t count, stopAt;
    unsigned frames;
};

static bool boundary(struct CupidGBA* gba, uint64_t step, void* argument) {
    struct Boundaries* p = argument;
    CHECK(step == p->count && step <= p->stopAt);
    struct BoundaryRecord* r = &p->records[p->count++];
    memcpy(r->registers, gba->cpu.gprs, sizeof(r->registers));
    memcpy(r->prefetch, gba->cpu.prefetch, sizeof(r->prefetch));
    r->cycles = gba->cpu.cycles; r->nextEvent = gba->cpu.nextEvent;
    r->cpsr = gba->cpu.cpsr.packed; r->spsr = gba->cpu.spsr.packed;
    r->bus = gba->bus; r->clock = cupid_timing_global_time(&gba->timing);
    r->frame = gba->video.frameCounter; r->samples = gba->audio.sampleWritePos;
    r->keys = gba->keysActive; r->ie = gba->ie; r->irqf = gba->irqf;
    r->halted = gba->cpu.halted; r->pending = gba->cpuInstructionPending;
    r->available = gba->irq.available; r->line = gba->irq.line;
    r->waiting = gba->timing.waitingForEvent; r->timed = gba->cpu.memory.timed;
    r->frameReady = gba->frameReady;
    if (gba->frameReady) { ++p->frames; gba->frameReady = false; }
    if (step == 1000) {
        gba->keysActive = 3;
        cupid_io_write(gba, REG_IE, IRQ_KEYPAD);
        cupid_raise_irq(gba, IRQ_KEYPAD, 0);
    }
    return step < p->stopAt;
}

static void same_queue(const struct CupidTimingEvent* a, const struct CupidTimingEvent* b) {
    while (a && b) {
        CHECK(a->when == b->when && a->priority == b->priority && a->callback == b->callback);
        CHECK((a->name == NULL) == (b->name == NULL));
        if (a->name) CHECK(!strcmp(a->name, b->name));
        a = a->next; b = b->next;
    }
    CHECK(!a && !b);
}

static void batch(void) {
    unsigned scenes = 0;
    for (unsigned wrap = 0; wrap < 2; ++wrap) for (unsigned thumb = 0; thumb < 2; ++thumb)
    for (unsigned halt = 0; halt < 2; ++halt) for (unsigned early = 0; early < 2; ++early) {
        struct CupidGBA* cores[] = {machine(thumb, wrap), machine(thumb, wrap)};
        const uint64_t limit = 1200, stopAt = early ? 17 : limit;
        struct Boundaries p[2] = {{.stopAt = stopAt}, {.stopAt = stopAt}};
        struct Observations seen[2] = {{0}, {0}};
        for (unsigned i = 0; i < 2; ++i) {
            p[i].records = calloc(stopAt + 1, sizeof(*p[i].records)); CHECK(p[i].records);
            cores[i]->cpu.instructionObserver = observe;
            cores[i]->cpu.instructionObserverContext = &seen[i];
            if (halt) request_halt(cores[i]);
        }
        CHECK(boundary(cores[0], 0, &p[0]));
        uint64_t steps;
        for (steps = 1; steps <= limit; ++steps) {
            cupid_gba_step(cores[0]);
            if (!boundary(cores[0], steps, &p[0])) break;
        }
        CHECK(steps == stopAt);
        CHECK(cupid_gba_run_steps(cores[1], limit, boundary, &p[1]) == steps);
        CHECK(p[0].count == p[1].count && p[0].frames == p[1].frames);
        CHECK(!memcmp(p[0].records, p[1].records, p[0].count * sizeof(*p[0].records)));
        CHECK(!memcmp(&seen[0], &seen[1], sizeof(seen[0])));
        CHECK(!memcmp(cores[0]->memory.ewram, cores[1]->memory.ewram, GBA_SIZE_EWRAM));
        CHECK(!memcmp(cores[0]->memory.iwram, cores[1]->memory.iwram, GBA_SIZE_IWRAM));
        CHECK(!memcmp(cores[0]->memory.io, cores[1]->memory.io, sizeof(cores[0]->memory.io)));
        CHECK(!memcmp(cores[0]->video.vram, cores[1]->video.vram, GBA_SIZE_VRAM));
        CHECK(!memcmp(cores[0]->video.outputBuffer, cores[1]->video.outputBuffer, 240 * 160 * sizeof(uint32_t)));
        same_queue(cores[0]->timing.root, cores[1]->timing.root);
        same_queue(cores[0]->timing.reroot, cores[1]->timing.reroot);
        if (halt && !early) CHECK(p[0].frames && seen[0].branch == 1 && seen[0].add == 1 && seen[0].store == 1);
        for (unsigned i = 0; i < 2; ++i) { free(p[i].records); destroy(cores[i]); }
        ++scenes;
    }
    CHECK(scenes == 16);
    struct CupidGBA* gba = machine(0, 0);
    struct BoundaryRecord records[2];
    memset(records, 0, sizeof(records));
    struct Boundaries p = {.records = records, .stopAt = 1};
    uint64_t before = cupid_timing_global_time(&gba->timing);
    CHECK(cupid_gba_run_steps(gba, 0, boundary, &p) == 0);
    CHECK(!p.count && !gba->execution && cupid_timing_global_time(&gba->timing) == before);
    request_halt(gba);
    CHECK(cupid_gba_run_steps(gba, 1, boundary, &p) == 1);
    CHECK(gba->cpuInstructionPending && gba->cpu.halted);
    /* The returned callback storage may die while the CPU fetch remains live.
     * A different request resumes that fetch without reusing its callback. */
    cupid_gba_run_frame(gba);
    CHECK(p.count == 2 && gba->cpu.halted && gba->frameReady);
    CHECK(cupid_gba_run_steps(gba, 3, NULL, NULL) == 3);
    CHECK(p.count == 2 && gba->cpuInstructionPending);
    cupid_gba_reset(gba);
    cupid_gba_stop(gba);
    before = cupid_timing_global_time(&gba->timing);
    CHECK(cupid_gba_run_steps(gba, 10, NULL, NULL) == 0);
    CHECK(gba->stopped && cupid_timing_global_time(&gba->timing) == before);
    destroy(gba);
    printf("Verified %u batched/repeated step boundary sequences, early stops, frame/input/wake publication, queues and cancellation\n", scenes);
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "frames")) frames();
    else if (!strcmp(argv[1], "interval")) interval();
    else if (!strcmp(argv[1], "wake")) wake();
    else if (!strcmp(argv[1], "lifetime")) lifetime();
    else if (!strcmp(argv[1], "ready-wake")) ready_wake();
    else if (!strcmp(argv[1], "batch")) batch();
    else if (!strcmp(argv[1], "cable")) cable();
    else return 2;
    printf("PASS suspended execution %s\n", argv[1]);
    return 0;
}
