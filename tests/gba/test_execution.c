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

int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "frames")) frames();
    else if (!strcmp(argv[1], "interval")) interval();
    else if (!strcmp(argv[1], "wake")) wake();
    else if (!strcmp(argv[1], "lifetime")) lifetime();
    else if (!strcmp(argv[1], "ready-wake")) ready_wake();
    else if (!strcmp(argv[1], "cable")) cable();
    else return 2;
    printf("PASS suspended execution %s\n", argv[1]);
    return 0;
}
