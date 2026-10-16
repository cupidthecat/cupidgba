#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"
#include "../bios/caller.h"

struct Observation {
    uint32_t address, opcode, r0;
    uint64_t clock;
    enum CupidExecMode mode;
    enum CupidPrivMode bank;
};
struct Trace { struct Observation records[32]; unsigned count; };

static void observe(const struct CupidARM* cpu, void* context) {
    struct Trace* trace = context;
    CHECK(trace->count < 32);
    const bool thumb = cpu->executionMode == MODE_THUMB;
    trace->records[trace->count++] = (struct Observation){
        .address = (uint32_t)cpu->gprs[ARM_PC] - (thumb ? 2u : 4u),
        .opcode = cpu->prefetch[0], .r0 = (uint32_t)cpu->gprs[0],
        .clock = cupid_timing_global_time(&cpu->master->timing),
        .mode = cpu->executionMode, .bank = cpu->privilegeMode};
}
static struct CupidGBA* machine(uint64_t clock) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba)); CHECK(gba);
    cupid_gba_create(gba); cupid_gba_reset(gba); cupid_test_bios_caller(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock; gba->timing.masterCycles = (uint32_t)clock;
    gba->cpu.cpsr.i = 1; gba->cpu.irqDisableLatch = true;
    return gba;
}
static void destroy(struct CupidGBA* gba) { cupid_gba_destroy(gba); free(gba); }
static void start(struct CupidGBA* gba, bool thumb) {
    const uint32_t arm[] = {0xE2800001, 0x02800009, 0xE5810000, 0xEAFFFFFE};
    const uint16_t narrow[] = {0x3001, 0x6008, 0x4612, 0xE7FE};
    memcpy(gba->memory.iwram, thumb ? (const void*)narrow : (const void*)arm, thumb ? sizeof(narrow) : sizeof(arm));
    gba->cpu.gprs[0] = 0; gba->cpu.gprs[1] = GBA_BASE_EWRAM;
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
    if (thumb) CupidThumbWritePC(&gba->cpu); else CupidARMWritePC(&gba->cpu);
}

static void streams_are_passive_and_include_failed_predicates(void) {
    const uint64_t clocks[] = {0, INT32_MAX - UINT64_C(4), UINT32_MAX - UINT64_C(4), UINT64_C(0x300000000)};
    const uint32_t arm[] = {0xE2800001, 0x02800009, 0xE5810000};
    const uint32_t thumbOps[] = {0x3001, 0x6008, 0x4612};
    const uint32_t inputs[] = {0, 1, 1};
    for (unsigned origin = 0; origin < 4; origin++) for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* plain = machine(clocks[origin]);
        struct CupidGBA* traced = machine(clocks[origin]);
        start(plain, thumb != 0); start(traced, thumb != 0);
        struct Trace trace = {0};
        traced->cpu.instructionObserver = observe; traced->cpu.instructionObserverContext = &trace;
        for (unsigned step = 0; step < 3; step++) {
            cupid_gba_step(plain); cupid_gba_step(traced);
            CHECK(trace.count == step + 1);
            const struct Observation* record = &trace.records[step];
            CHECK(record->address == GBA_BASE_IWRAM + step * (thumb ? 2u : 4u));
            CHECK(record->opcode == (thumb ? thumbOps[step] : arm[step]) && record->r0 == inputs[step]);
            CHECK(record->mode == (thumb ? MODE_THUMB : MODE_ARM) && record->bank == MODE_SYS);
            if (step) CHECK(record->clock > trace.records[step - 1].clock);
            CHECK(!memcmp(plain->cpu.gprs, traced->cpu.gprs, sizeof(plain->cpu.gprs)));
            CHECK(plain->cpu.cpsr.packed == traced->cpu.cpsr.packed);
            CHECK(!memcmp(plain->cpu.prefetch, traced->cpu.prefetch, sizeof(plain->cpu.prefetch)));
            CHECK(!memcmp(plain->memory.ewram, traced->memory.ewram, 4));
            CHECK(cupid_timing_global_time(&plain->timing) == cupid_timing_global_time(&traced->timing));
        }
        uint32_t stored; memcpy(&stored, traced->memory.ewram, 4); CHECK(stored == 1);
        traced->cpu.instructionObserver = NULL;
        cupid_gba_step(plain); cupid_gba_step(traced);
        CHECK(trace.count == 3 && cupid_timing_global_time(&plain->timing) == cupid_timing_global_time(&traced->timing));
        destroy(plain); destroy(traced);
    }
}

static void arbitration_refills_and_reset_do_not_fabricate_issue(void) {
    struct CupidARM fresh;
    memset(&fresh, 0xA5, sizeof(fresh)); cupid_arm_init(&fresh);
    CHECK(!fresh.instructionObserver && !fresh.instructionObserverContext);
    struct CupidGBA* gba = machine(0);
    start(gba, false); struct Trace trace = {0};
    gba->cpu.instructionObserver = observe; gba->cpu.instructionObserverContext = &trace;
    /* A branch refill fetches two opcodes but issues only the branch itself. */
    const uint32_t branch[] = {0xEA000000, 0xE3A00009, 0xE3A00007, 0xEAFFFFFE};
    memcpy(gba->memory.iwram, branch, sizeof(branch));
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM; CupidARMWritePC(&gba->cpu);
    CHECK(!trace.count);
    cupid_gba_step(gba); cupid_gba_step(gba);
    CHECK(trace.count == 2 && trace.records[0].address == GBA_BASE_IWRAM && trace.records[1].address == GBA_BASE_IWRAM + 8);
    CHECK(gba->cpu.gprs[0] == 7);
    trace.count = 0;
    /* IRQ entry replaces the pending application opcode. Only the first
     * vector instruction, issued after arbitration, reaches the observer. */
    start(gba, false); gba->cpu.cpsr.i = 0; gba->cpu.irqDisableLatch = false; gba->irq.line = true;
    cupid_gba_step(gba);
    uint32_t vector; memcpy(&vector, gba->memory.bios + 0x18, 4);
    CHECK(trace.count == 1 && trace.records[0].address == 0x18 && trace.records[0].opcode == vector);
    CHECK(trace.records[0].bank == MODE_IRQ && trace.records[0].mode == MODE_ARM);
    cupid_gba_reset(gba);
    CHECK(gba->cpu.instructionObserver == observe && gba->cpu.instructionObserverContext == &trace && trace.count == 1);
    cupid_timing_clear(&gba->timing); cupid_test_bios_caller(gba);
    gba->cpu.cpsr.i = 1; gba->cpu.irqDisableLatch = true; start(gba, false); trace.count = 0;
    cupid_gba_halt(gba);
    for (unsigned step = 0; step < 4; step++) {
        gba->cpu.nextEvent = 1; cupid_gba_step(gba);
        CHECK(gba->cpu.halted && !trace.count);
    }
    cupid_io_write(gba, REG_IE, IRQ_KEYPAD); cupid_timing_tick(&gba->timing, 1);
    cupid_gba_stop(gba); CHECK(gba->stopped);
    const uint64_t stopped = cupid_timing_global_time(&gba->timing);
    for (unsigned step = 0; step < 4; step++) cupid_gba_step(gba);
    CHECK(!trace.count && cupid_timing_global_time(&gba->timing) == stopped);
    cupid_raise_irq_at(gba, IRQ_KEYPAD, stopped);
    CHECK(!gba->stopped);
    cupid_timing_tick(&gba->timing, 3);
    cupid_gba_step(gba);
    CHECK(trace.count == 1 && trace.records[0].address == GBA_BASE_IWRAM && trace.records[0].r0 == 0);
    destroy(gba);
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "stream")) RUN(streams_are_passive_and_include_failed_predicates);
    else if (!strcmp(argv[1], "boundaries")) RUN(arbitration_refills_and_reset_do_not_fabricate_issue);
    else CHECK(false);
    return 0;
}
