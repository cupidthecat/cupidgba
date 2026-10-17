/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../../test.h"
#include <errno.h>

static const uint64_t epochs[] = {0, INT32_MAX - 19u, UINT32_MAX - 19u, UINT64_C(0x300000000)};
/* Typical 14 us, 18 ms and 70 ms, rounded up at 16,777,216 Hz. */
static const uint32_t durations[] = {235, 301990, 1174406};

static void command(struct CupidSavedata* save, uint8_t value) {
    cupid_savedata_write_flash(save, 0x5555, 0xAA);
    cupid_savedata_write_flash(save, 0x2AAA, 0x55);
    cupid_savedata_write_flash(save, 0x5555, value);
}

static struct CupidGBA* machine(uint64_t epoch) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = epoch;
    gba->timing.masterCycles = (uint32_t)epoch;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    cupid_savedata_force_type(&gba->memory.savedata, SAVE_FLASH512);
    CHECK(cupid_savedata_set_flash_profile(&gba->memory.savedata, FLASH_PROFILE_SST39VF512));
    return gba;
}

static void destroy(struct CupidGBA* gba) { cupid_gba_destroy(gba); free(gba); }

static void rejected(struct CupidSavedata* save, enum CupidFlashProfile profile) {
    struct CupidSavedata before;
    memcpy(&before, save, sizeof(before));
    CHECK(!cupid_savedata_set_flash_profile(save, profile));
    CHECK(!memcmp(&before, save, sizeof(before)));
}

static void selection(void) {
    struct CupidSavedata save;
    cupid_savedata_init(&save);
    rejected(&save, FLASH_PROFILE_SST39VF512);
    const enum CupidSaveType invalid[] = {SAVE_NONE, SAVE_SRAM, SAVE_SRAM512, SAVE_EEPROM, SAVE_EEPROM512, SAVE_FLASH1M};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        cupid_savedata_force_type(&save, invalid[i]);
        rejected(&save, FLASH_PROFILE_SST39VF512);
    }
    cupid_savedata_force_type(&save, SAVE_FLASH512);
    CHECK(cupid_savedata_set_programming_clocks(&save, 17, 19, 23, 29));
    CHECK(cupid_savedata_set_flash_profile(&save, FLASH_PROFILE_SST39VF512));
    CHECK(save.typeForced && save.size == 65536 && save.flashBank == 0);
    CHECK(save.eepromProgramClocks == 17 && save.flashProgramClocks == 235);
    CHECK(save.flashSectorEraseClocks == 301990 && save.flashChipEraseClocks == 1174406);
    rejected(&save, (enum CupidFlashProfile)99);
    cupid_savedata_write_flash(&save, 0x5555, 0xAA);
    rejected(&save, FLASH_PROFILE_DEFAULT);
    cupid_savedata_write_flash(&save, 0x4A00, 0xF0);
    command(&save, 0x90);
    CHECK(cupid_savedata_read_flash(&save, 0) == 0xBF);
    CHECK(cupid_savedata_read_flash(&save, 1) == 0xD4);
    rejected(&save, FLASH_PROFILE_DEFAULT);
    cupid_savedata_write_flash(&save, 0x0400, 0xF0);
    CHECK(cupid_savedata_read_flash(&save, 0) == 0xFF);
    command(&save, 0x90);
    command(&save, 0xF0);
    CHECK(cupid_savedata_read_flash(&save, 1) == 0xFF);
    command(&save, 0xB0);
    cupid_savedata_write_flash(&save, 0, 1);
    /* An unsupported bank command must not consume the next unlock byte. */
    command(&save, 0x90);
    CHECK(cupid_savedata_read_flash(&save, 0) == 0xBF && save.size == 65536);
    cupid_savedata_write_flash(&save, 0xFFFF, 0xF0);
    CHECK(save.flashBank == 0 && save.currentBank == save.data);
    command(&save, 0xA0);
    cupid_savedata_write_flash(&save, 0x77, 0xF0);
    CHECK(save.programming && save.data[0x77] == 0xFF);
    rejected(&save, FLASH_PROFILE_DEFAULT);
    cupid_savedata_write_flash(&save, 0, 0xF0);
    CHECK(save.programming && save.readyClock == 235);
    cupid_savedata_advance_clock(&save, 234);
    CHECK(save.programming && !save.dirty);
    cupid_savedata_advance_clock(&save, 1);
    CHECK(!save.programming && save.data[0x77] == 0xF0 && save.dirty);
    cupid_savedata_force_type(&save, SAVE_FLASH512);
    CHECK(save.flashProfile == FLASH_PROFILE_SST39VF512);
    CHECK(cupid_savedata_set_flash_profile(&save, FLASH_PROFILE_DEFAULT));
    CHECK(save.typeForced && save.eepromProgramClocks == 17 && save.flashProgramClocks == 650);
    CHECK(save.flashSectorEraseClocks == 30000 && save.flashChipEraseClocks == 30000);
    command(&save, 0x90);
    CHECK(cupid_savedata_read_flash(&save, 0) == 0x32 && cupid_savedata_read_flash(&save, 1) == 0x1B);
    cupid_savedata_deinit(&save);
    CHECK(save.flashProfile == FLASH_PROFILE_DEFAULT);
    puts("PASS Flash profile selection, complete rejection state, identity, both exits and capacity");
}

static uint8_t initial(unsigned operation, unsigned address) {
    return operation ? (uint8_t)(address * 17 + 0x29) : 0xFF;
}

static void accept(struct CupidSavedata* save, unsigned operation) {
    for (unsigned i = 0; i < 65536; i++) save->data[i] = initial(operation, i);
    if (!operation) {
        command(save, 0xA0);
        cupid_savedata_write_flash(save, 0x2345, 0x25);
    } else {
        command(save, 0x80);
        cupid_savedata_write_flash(save, 0x5555, 0xAA);
        cupid_savedata_write_flash(save, 0x2AAA, 0x55);
        /* A sector address may equal the ordinary command address. */
        cupid_savedata_write_flash(save, 0x5555, operation == 1 ? 0x30 : 0x10);
    }
    CHECK(save->programming && !save->dirty);
}

static void image(struct CupidSavedata* save, unsigned operation, bool ready) {
    for (unsigned i = 0; i < 65536; i++) {
        uint8_t wanted = initial(operation, i);
        if (ready && ((!operation && i == 0x2345) || (operation == 1 && i >= 0x5000 && i < 0x6000) || operation == 2))
            wanted = operation ? 0xFF : 0x25;
        CHECK(save->data[i] == wanted);
    }
    CHECK(save->dirty == ready && save->programming != ready);
}

static void polling(struct CupidSavedata* save, unsigned operation) {
    const uint16_t addresses[] = {0, 0x2345, 0x5555, 0xA123, 0xFFFF};
    for (unsigned i = 0; i < 5; i++) {
        uint8_t first = cupid_savedata_read_flash(save, addresses[i]);
        uint8_t second = cupid_savedata_read_flash(save, addresses[(i + 1) % 5]);
        CHECK((first & 0x80) == (operation ? 0 : 0x80));
        CHECK((second & 0x80) == (first & 0x80));
        CHECK(((first ^ second) & 0x40) == 0x40);
    }
}

static void edges(void) {
    unsigned scenes = 0;
    for (unsigned e = 0; e < 4; e++) for (unsigned op = 0; op < 3; op++)
    for (unsigned late = 0; late < 2; late++) {
        struct CupidGBA* gba = machine(epochs[e]);
        struct CupidSavedata* save = &gba->memory.savedata;
        accept(save, op);
        CHECK(save->readyClock == epochs[e] + durations[op]);
        polling(save, op);
        image(save, op, false);
        uint64_t deadline = save->readyClock;
        command(save, 0x90); command(save, 0x80); command(save, 0x10);
        cupid_savedata_write_flash(save, 0, 0xF0);
        CHECK(save->readyClock == deadline && save->command == FLASH_COMMAND_NONE);
        cupid_timing_tick(&gba->timing, (int32_t)durations[op] - 1);
        polling(save, op);
        image(save, op, false);
        cupid_timing_tick(&gba->timing, late ? 8 : 1);
        image(save, op, true);
        CHECK(!cupid_timing_is_scheduled(&gba->timing, &save->readyEvent));
        CHECK(cupid_savedata_read_flash(save, 0x2345) == (op == 2 ? 0xFF : op ? initial(op, 0x2345) : 0x25));
        CHECK(cupid_savedata_read_flash(save, 0x5555) == 0xFF);
        destroy(gba);
        scenes++;
    }
    CHECK(scenes == 24);
    puts("PASS 24 complete Flash images, nominal ready edges, late dispatch, whole-chip polling and clock wrap");
}

static void cpu(void) {
    /* Actual ARM STRB / Thumb STRB instructions perform all four command
     * strobes. Poll loads run across the nominal edge with a timer active. */
    for (unsigned e = 0; e < 4; e++) for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine(epochs[e]);
        struct CupidSavedata* save = &gba->memory.savedata;
        const uint16_t half[] = {0x7008, 0x781C, 0xE7FE}; /* strb r0,[r1]; ldrb r4,[r3]; b . */
        const uint32_t word[] = {0xE5C10000, 0xE5D34000, 0xEAFFFFFE};
        memcpy(gba->memory.iwram, thumb ? (const void*)half : (const void*)word, thumb ? sizeof(half) : sizeof(word));
        const uint32_t addresses[] = {0x0E005555, 0x0E002AAA, 0x0E005555, 0x0E002345};
        const uint8_t values[] = {0xAA, 0x55, 0xA0, 0x25};
        for (unsigned i = 0; i < 4; i++) {
            gba->cpu.gprs[0] = values[i]; gba->cpu.gprs[1] = addresses[i];
            gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
            _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
            gba->cpu.cpsr.t = thumb;
            if (thumb) CupidThumbWritePC(&gba->cpu); else CupidARMWritePC(&gba->cpu);
            cupid_gba_step(gba);
            cupid_gba_process_events(gba);
        }
        const uint64_t accepted = cupid_timing_global_time(&gba->timing);
        CHECK(save->programming && save->readyClock == accepted + 235 && !save->dirty);
        CHECK(save->data[0x2345] == 0xFF);
        cupid_io_write(gba, REG_TM0CNT_L, 0xFF00);
        cupid_io_write(gba, REG_TM0CNT_H, 0xC0);
        gba->cpu.gprs[3] = 0x0E002345;
        for (unsigned n = 0; n < 2; n++) {
            gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + (thumb ? 2 : 4);
            if (thumb) CupidThumbWritePC(&gba->cpu); else CupidARMWritePC(&gba->cpu);
            cupid_gba_step(gba);
            cupid_gba_process_events(gba);
            CHECK(((unsigned)gba->cpu.gprs[4] & 0x80) == 0x80 && save->programming);
        }
        /* The register commits at +1 and enables counting from +2. */
        CHECK(gba->timers[0].event.when == (uint32_t)(accepted + 258));
        uint64_t clock = cupid_timing_global_time(&gba->timing);
        CHECK(clock < save->readyClock);
        cupid_timing_tick(&gba->timing, (int32_t)(save->readyClock - clock - 1));
        CHECK(save->programming && !save->dirty && !((gba->irq.pendingIF | gba->irqf) & IRQ_TIMER0));
        cupid_timing_tick(&gba->timing, 1);
        CHECK(!save->programming && save->data[0x2345] == 0x25 && save->dirty);
        gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + (thumb ? 2 : 4);
        if (thumb) CupidThumbWritePC(&gba->cpu); else CupidARMWritePC(&gba->cpu);
        cupid_gba_step(gba);
        cupid_gba_process_events(gba);
        CHECK(gba->cpu.gprs[4] == 0x25);
        clock = cupid_timing_global_time(&gba->timing);
        CHECK(clock < accepted + 257);
        cupid_timing_tick(&gba->timing, (int32_t)(accepted + 257 - clock));
        CHECK(!((gba->irq.pendingIF | gba->irqf) & IRQ_TIMER0));
        cupid_timing_tick(&gba->timing, 1);
        CHECK((gba->irq.pendingIF | gba->irqf) & IRQ_TIMER0);
        destroy(gba);
    }
    puts("PASS eight ARM/Thumb Flash command and poll streams with independent timer deadline");
}

struct DMAScene {
    struct CupidGBA* gba;
    uint64_t start, ready;
    uint32_t offset, length;
    unsigned operation, observations;
    uint8_t sampled;
};

static void dma_observe(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct DMAScene* scene = context;
    struct CupidGBA* gba = scene->gba;
    struct CupidSavedata* save = &gba->memory.savedata;
    const uint64_t clock = cupid_timing_global_time(timing);
    const unsigned phase = (unsigned)(clock - scene->start);
    const bool ready = clock >= scene->ready;
    CHECK(phase >= 7 && phase <= 10);
    CHECK(save->programming != ready && save->dirty == ready);
    for (size_t i = 0; i < save->size; i++) {
        uint8_t wanted = scene->operation ? 0x5A : 0xFF;
        if (ready && i >= scene->offset && i - scene->offset < scene->length)
            wanted = scene->operation ? 0xFF : 0x25;
        CHECK(save->data[i] == wanted);
    }
    if (phase == 7) {
        CHECK(gba->memory.dma[3].latch == 0 && gba->cpuBlocked);
    } else {
        CHECK(gba->memory.dma[3].latch == (uint32_t)scene->sampled * UINT32_C(0x01010101));
    }
    for (unsigned i = 0x30; i < 0x50; i++) {
        uint8_t wanted = phase >= 9 && i >= 0x40 && i < 0x42 ? scene->sampled : 0xCC;
        CHECK(gba->memory.iwram[i] == wanted);
    }
    const unsigned irq = gba->irq.pendingIF | gba->irqf;
    CHECK(((irq & IRQ_DMA3) != 0) == (phase >= 9));
    CHECK(((irq & IRQ_TIMER0) != 0) == (phase >= 10));
    CHECK(gba->cpuBlocked == (phase < 10));
    scene->observations++;
}

static void dma(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++) for (unsigned profile = 0; profile < 3; profile++)
    for (unsigned operation = 0; operation < 3; operation++) for (int edge = -1; edge <= 1; edge++)
    for (unsigned late = 0; late < 2; late++) {
        struct CupidGBA* gba = machine(epochs[epoch]);
        struct CupidSavedata* save = &gba->memory.savedata;
        if (profile != 0) {
            CHECK(cupid_savedata_set_flash_profile(save, FLASH_PROFILE_DEFAULT));
            if (profile == 2) {
                cupid_savedata_force_type(save, SAVE_FLASH1M);
                command(save, 0xB0); cupid_savedata_write_flash(save, 0, 1);
            }
        }
        const uint32_t bank = profile == 2 ? 0x10000 : 0;
        const uint16_t address = operation == 0 ? 0x2344 : operation == 1 ? 0x5000 : 0;
        const uint32_t offset = operation == 2 ? 0 : bank + address;
        const uint32_t length = operation == 0 ? 1 : operation == 1 ? 4096 : (uint32_t)save->size;
        const uint32_t duration = profile == 0 ? durations[operation] : operation == 0 ? 650 : 30000;
        memset(save->data, operation ? 0x5A : 0xFF, save->size);
        if (operation == 0) {
            command(save, 0xA0); cupid_savedata_write_flash(save, address, 0x25);
        } else {
            command(save, 0x80);
            if (operation == 2) command(save, 0x10);
            else {
                cupid_savedata_write_flash(save, 0x5555, 0xAA);
                cupid_savedata_write_flash(save, 0x2AAA, 0x55);
                cupid_savedata_write_flash(save, address, 0x30);
            }
        }
        CHECK(save->programming && save->readyClock == epochs[epoch] + duration);
        /* This bus model requests DMA at +2, enters its head phase at +3,
         * samples the default five-clock save access at +8, writes IWRAM at
         * +9 and releases the CPU at +10. The ready edge straddles that read. */
        CHECK(gba->memory.waitstatesSeq16[GBA_REGION_SRAM] == 4);
        const uint64_t start = save->readyClock + edge - 8;
        cupid_timing_tick(&gba->timing, (int32_t)(start - epochs[epoch]));
        cupid_timer_write_reload(gba, 0, 0xFFF8);
        cupid_timer_write_control(gba, 0, 0xC0);
        memset(gba->memory.iwram + 0x30, 0xCC, 0x20);
        cupid_dma_write_sad(gba, 3, GBA_BASE_SRAM + address);
        cupid_dma_write_dad(gba, 3, GBA_BASE_IWRAM + 0x40);
        cupid_dma_write_cnt_lo(gba, 3, 1);
        cupid_dma_write_cnt(gba, 3, 0xC000);
        struct DMAScene scene = {.gba = gba, .start = start, .ready = save->readyClock,
            .offset = offset, .length = length, .operation = operation,
            .sampled = edge < 0 ? (operation ? 0 : 0x80) : (operation ? 0xFF : 0x25)};
        struct CupidTimingEvent observations[4];
        for (unsigned i = 0; i < 4; i++) {
            observations[i] = (struct CupidTimingEvent){.callback = dma_observe, .context = &scene, .priority = 1};
            cupid_timing_schedule(&gba->timing, &observations[i], (int32_t)i + 7);
        }
        if (late) cupid_timing_tick(&gba->timing, 12);
        else for (unsigned i = 0; i < 12; i++) cupid_timing_tick(&gba->timing, 1);
        CHECK(scene.observations == 4 && !save->programming && save->dirty);
        CHECK(!gba->memory.dmaRunning && !gba->cpuBlocked && gba->memory.activeDMA == -1);
        destroy(gba);
        scenes++;
    }
    CHECK(scenes == 216);
    puts("PASS 216 complete Flash DMA/timer scenes: before/at/after ready, captured read data, IRQ/release order, late dispatch and clock wrap");
}

static void lifecycle(const char* directory) {
    /* The build directory owns this fixture's save image. */
    char path[1024];
    CHECK(snprintf(path, sizeof(path), "%s/flash-profile-%p.sav", directory, (void*)directory) > 0);
    struct CupidGBA* gba = machine(UINT32_MAX - 19u);
    struct CupidSavedata* save = &gba->memory.savedata;
    accept(save, 0);
    cupid_timing_tick(&gba->timing, 10);
    uint64_t remaining = save->readyClock - cupid_timing_global_time(&gba->timing);
    cupid_gba_reset(gba);
    CHECK(save->flashProfile == FLASH_PROFILE_SST39VF512 && save->programming);
    CHECK(save->readyClock - cupid_timing_global_time(&gba->timing) == remaining);
    cupid_timing_tick(&gba->timing, (int32_t)remaining);
    image(save, 0, true);
    CHECK(cupid_savedata_save_file(save, path));
    command(save, 0xA0); cupid_savedata_write_flash(save, 0x2346, 0x5A);
    CHECK(save->programming && cupid_savedata_load_file(save, path));
    CHECK(save->flashProfile == FLASH_PROFILE_SST39VF512 && save->typeForced);
    CHECK(!save->programming && !save->dirty && save->data[0x2346] == 0xFF);
    FILE* file = fopen(path, "wb"); CHECK(file);
    for (unsigned i = 0; i < 131072; i++) CHECK(fputc(0x3C, file) == 0x3C);
    CHECK(!fclose(file));
    command(save, 0xA0); cupid_savedata_write_flash(save, 0x2346, 0x5A);
    uint64_t deadline = save->readyClock;
    CHECK(!cupid_savedata_load_file(save, path) && errno == EINVAL);
    CHECK(save->programming && save->readyClock == deadline && save->size == 65536);
    CHECK(save->flashProfile == FLASH_PROFILE_SST39VF512 && save->data[0x2346] == 0xFF);
    cupid_savedata_force_type(save, SAVE_EEPROM512);
    CHECK(save->flashProfile == FLASH_PROFILE_DEFAULT && !save->programming);
    CHECK(!cupid_timing_is_scheduled(&gba->timing, &save->readyEvent));
    destroy(gba);
    CHECK(!remove(path));
    puts("PASS Flash profile reset, compatible replacement, oversized-load rejection and destruction");
}

int main(int argc, char** argv) {
    CHECK(argc >= 2);
    if (!strcmp(argv[1], "selection")) selection();
    else if (!strcmp(argv[1], "edges")) edges();
    else if (!strcmp(argv[1], "cpu")) cpu();
    else if (!strcmp(argv[1], "dma")) dma();
    else if (!strcmp(argv[1], "lifecycle")) { CHECK(argc == 3); lifecycle(argv[2]); }
    else CHECK(false);
    return 0;
}
