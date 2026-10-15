/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"
#include "caller.h"
#include <inttypes.h>

static const char* externalBios;
static const enum CupidPrivMode callers[] = {MODE_USR, MODE_FIQ, MODE_IRQ, MODE_SVC, MODE_ABT, MODE_UND, MODE_SYS};
static const uint64_t anchors[] = {0, UINT32_MAX - 100u};
static unsigned observations;
static uint8_t externalImage[GBA_SIZE_BIOS];
static const char* imagePath;
static uint32_t imageChecksum;

static uint32_t word(const uint8_t* data) {
    uint32_t value;
    memcpy(&value, data, 4);
    return value;
}

static struct CupidGBA* machine(const char* bios, uint64_t clock) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    if (bios) {
        if (!imagePath || strcmp(imagePath, bios)) {
            CHECK(cupid_load_bios(gba, bios));
            memcpy(externalImage, gba->memory.bios, sizeof(externalImage));
            imageChecksum = gba->biosChecksum;
            imagePath = bios;
        } else {
            memcpy(gba->memory.bios, externalImage, sizeof(externalImage));
            gba->memory.fullBios = 1;
            gba->memory.biosLoaded = true;
            gba->hasBios = true;
            gba->biosHLE = false;
            gba->biosChecksum = imageChecksum;
        }
    }
    cupid_gba_reset(gba);
    cupid_test_bios_caller(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
    cupid_video_reset(&gba->video);
    cupid_audio_reset(&gba->audio);
    /* Move the fresh mixer's epoch with the synthetic wrap-test clock. */
    gba->audio.lastSample = clock;
    gba->audio.nextSample = clock + 512;
    gba->audio.sampleOrigin = clock;
    gba->memory.rom = calloc(1, 4096);
    CHECK(gba->memory.rom);
    gba->memory.romSize = 4096;
    gba->memory.romMask = 4095;
    const uint32_t entry[] = {0xE3A080A1, 0xEAFFFFFE};
    memcpy(gba->memory.rom, entry, sizeof(entry));
    memset(gba->memory.ewram, 0x3C, GBA_SIZE_EWRAM);
    memset(gba->memory.iwram, 0x5A, GBA_SIZE_IWRAM);
    memset(gba->video.vram, 0x69, GBA_SIZE_VRAM);
    memset(gba->video.palette, 0x69, GBA_SIZE_PALETTE);
    memset(gba->video.oam.raw, 0x69, GBA_SIZE_OAM);
    /* Application caller state is injected without running a firmware boot. */
    gba->memory.io[REG_POSTFLG >> 1] = 1;
    cupid_savedata_force_type(&gba->memory.savedata, SAVE_SRAM);
    memset(gba->memory.savedata.data, 0xA7, gba->memory.savedata.size);
    gba->memory.savedata.dirty = false;
    return gba;
}

static void destroy(struct CupidGBA* gba) { cupid_gba_destroy(gba); free(gba); }

static void prepare(struct CupidGBA* gba, bool thumb, unsigned bank, unsigned masks, unsigned region) {
    const uint32_t arm[] = {0xEF260000, 0xE3A080CC, 0xEAFFFFFE};
    const uint16_t narrow[] = {0xDF26, 0x20CC, 0xE7FE};
    uint8_t* destinations[] = {gba->memory.iwram, gba->memory.ewram, gba->memory.rom};
    const uint32_t addresses[] = {GBA_BASE_IWRAM, GBA_BASE_EWRAM, GBA_BASE_ROM0};
    CHECK(bank < 7 && masks < 4 && region < 3);
    memcpy(destinations[region] + 0x100, thumb ? (const void*)narrow : (const void*)arm,
           thumb ? sizeof(narrow) : sizeof(arm));
    cupid_arm_set_priv_mode(&gba->cpu, callers[bank]);
    for (unsigned r = 0; r < 15; r++) gba->cpu.gprs[r] = (int32_t)(0x11220000u + r);
    uint32_t stack = callers[bank] == MODE_SVC ? GBA_SP_BASE_SUPERVISOR :
                     callers[bank] == MODE_IRQ ? GBA_SP_BASE_IRQ :
                     callers[bank] == MODE_FIQ ? GBA_BASE_IWRAM + 0x7C00 :
                     callers[bank] == MODE_ABT ? GBA_BASE_IWRAM + 0x7B80 :
                     callers[bank] == MODE_UND ? GBA_BASE_IWRAM + 0x7B00 : GBA_SP_BASE_SYSTEM;
    gba->cpu.gprs[ARM_SP] = (int32_t)stack;
    gba->cpu.cpsr.packed = (int32_t)(0xA0000000u | callers[bank] | masks * 0x40u);
    gba->cpu.irqDisableLatch = (masks & 2) != 0;
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = (int32_t)(addresses[region] + 0x100);
    if (thumb) CupidThumbWritePC(&gba->cpu); else CupidARMWritePC(&gba->cpu);
    cupid_io_write(gba, REG_WAITCNT, region == 2 ? 0x4317 : 0);
    cupid_io_write(gba, REG_TM0CNT_L, 0);
    cupid_io_write(gba, REG_TM0CNT_H, 0x80);
    cupid_timing_tick(&gba->timing, 2);
}

static bool startup_entry(const struct CupidGBA* gba) {
    uint32_t handler = word(gba->memory.iwram + 0x7FFC);
    return handler && handler < GBA_SIZE_BIOS && !(handler & 3) &&
           gba->cpu.executionMode == MODE_THUMB &&
           gba->cpu.gprs[ARM_SP] == GBA_SP_BASE_SYSTEM;
}

struct Prefix {
    uint64_t clocks;
    uint32_t cpsr, registers[11], banks[6][7], saved[6];
    uint16_t timer, post, ime;
    uint8_t work[0x1FC];
};

static struct Prefix prefix(const char* bios, bool thumb, unsigned bank, unsigned masks,
                            unsigned region, uint64_t clock) {
    struct CupidGBA* gba = machine(bios, clock);
    prepare(gba, thumb, bank, masks, region);
    uint64_t before = cupid_timing_global_time(&gba->timing);
    unsigned steps;
    for (steps = 0; steps < 2000 && !startup_entry(gba); steps++) {
        cupid_gba_step(gba);
        CHECK(!gba->stopped && !gba->cpu.halted);
        if ((uint32_t)gba->cpu.gprs[ARM_PC] >= GBA_BASE_EWRAM)
            fprintf(stderr, "Unexpected prefix PC: step=%u pc=%08x opcode=%08x mode=%u bank=%u masks=%u region=%u\n",
                    steps, gba->cpu.gprs[ARM_PC], gba->cpu.prefetch[0], thumb, bank, masks, region);
        CHECK((uint32_t)gba->cpu.gprs[ARM_PC] < GBA_BASE_EWRAM);
    }
    CHECK(steps < 2000 && startup_entry(gba));
    struct Prefix result = {0};
    result.clocks = cupid_timing_global_time(&gba->timing) - before;
    result.cpsr = (uint32_t)gba->cpu.cpsr.packed;
    for (unsigned r = 1; r < 12; r++) result.registers[r - 1] = (uint32_t)gba->cpu.gprs[r];
    memcpy(result.banks, gba->cpu.bankedRegisters, sizeof(result.banks));
    /* The inactive System LR shadow holds the return after the first bank
     * reset call. Compare that position relative to its own service entry. */
    uint32_t table = bios ? 0x1C8 : 0x198;
    uint32_t service = word(gba->memory.bios + table + 0x26 * 4);
    CHECK(result.banks[0][1] == service + 20);
    result.banks[0][1] -= service;
    memcpy(result.saved, gba->cpu.bankedSPSRs, sizeof(result.saved));
    result.timer = cupid_timer_read_counter(gba, 0);
    result.post = gba->memory.io[REG_POSTFLG >> 1];
    result.ime = gba->ime;
    memcpy(result.work, gba->memory.iwram + 0x7E00, sizeof(result.work));
    static const uint16_t prefixClocks[2][3] = {{844, 851, 850}, {844, 848, 848}};
    CHECK(result.clocks == prefixClocks[thumb ? 1 : 0][region]);
    CHECK(result.cpsr == 0x6000007F && result.post == 1 && !result.ime);
    CHECK(gba->cpu.privilegeMode == MODE_SYS && gba->cpu.gprs[4] == GBA_BASE_IO);
    CHECK(result.timer == (uint16_t)result.clocks);
    for (unsigned i = 0; i < sizeof(result.work); i++) CHECK(!result.work[i]);
    CHECK(gba->memory.iwram[0x100] == (region == 0 ? (thumb ? 0x26 : 0) : 0x5A));
    destroy(gba);
    return result;
}

static void prefix_matrix(unsigned masks) {
    for (unsigned bank = 0; bank < 7; bank++) for (unsigned thumb = 0; thumb < 2; thumb++)
        for (unsigned region = 0; region < 3; region++) for (unsigned c = 0; c < 2; c++) {
            struct Prefix built = prefix(NULL, thumb != 0, bank, masks, region, anchors[c]);
            if (externalBios) {
                struct Prefix firmware = prefix(externalBios, thumb != 0, bank, masks, region, anchors[c]);
                bool same = built.clocks == firmware.clocks && built.cpsr == firmware.cpsr &&
                    built.timer == firmware.timer && built.post == firmware.post && built.ime == firmware.ime &&
                    !memcmp(built.registers, firmware.registers, sizeof(built.registers)) &&
                    !memcmp(built.banks, firmware.banks, sizeof(built.banks)) &&
                    !memcmp(built.saved, firmware.saved, sizeof(built.saved)) &&
                    !memcmp(built.work, firmware.work, sizeof(built.work));
                if (!same)
                    fprintf(stderr, "Hard reset prefix mismatch: bank=%u thumb=%u masks=%u region=%u clocks=%" PRIu64 "/%" PRIu64 "\n",
                            bank, thumb, masks, region, built.clocks, firmware.clocks);
                CHECK(same);
            }
            observations++;
        }
    CHECK(observations == 84);
}

static uint64_t complete(struct CupidGBA* gba) {
    uint64_t before = cupid_timing_global_time(&gba->timing);
    unsigned steps;
    for (steps = 0; steps < 500000; steps++) {
        if (gba->cpu.executionMode == MODE_ARM && gba->cpu.gprs[ARM_PC] == GBA_BASE_ROM0 + 4) break;
        cupid_gba_step(gba);
        CHECK(!gba->stopped && !gba->cpu.halted);
    }
    CHECK(steps < 500000);
    CHECK(gba->cpu.privilegeMode == MODE_SYS && gba->cpu.cpsr.packed == MODE_SYS);
    CHECK(gba->cpu.prefetch[0] == 0xE3A080A1 && gba->cpu.prefetch[1] == 0xEAFFFFFE);
    for (unsigned r = 0; r < 13; r++) CHECK(!gba->cpu.gprs[r]);
    CHECK(gba->cpu.gprs[ARM_SP] == GBA_SP_BASE_SYSTEM && gba->cpu.gprs[ARM_LR] == GBA_BASE_ROM0);
    CHECK(gba->memory.io[REG_POSTFLG >> 1] == 1 && !gba->ime && !gba->ie && !gba->irqf);
    CHECK(!(gba->timers[0].control & 0x80) && !gba->timers[0].running);
    for (unsigned i = 0; i < GBA_SIZE_EWRAM; i++) CHECK(!gba->memory.ewram[i]);
    for (unsigned i = 0; i < GBA_SIZE_IWRAM; i++) CHECK(!gba->memory.iwram[i]);
    const uint8_t* vram = (const uint8_t*)gba->video.vram;
    for (unsigned i = 0; i < GBA_SIZE_VRAM; i++) CHECK(!vram[i]);
    const uint8_t* palette = (const uint8_t*)gba->video.palette;
    const uint8_t* objects = (const uint8_t*)gba->video.oam.raw;
    for (unsigned i = 0; i < GBA_SIZE_PALETTE; i++) CHECK(!palette[i]);
    for (unsigned i = 0; i < GBA_SIZE_OAM; i++) CHECK(!objects[i]);
    for (unsigned i = 0; i < gba->memory.savedata.size; i++) CHECK(gba->memory.savedata.data[i] == 0xA7);
    CHECK(!gba->memory.savedata.dirty && gba->memory.savedata.type == SAVE_SRAM);
    return cupid_timing_global_time(&gba->timing) - before;
}

static void complete_matrix(unsigned group) {
    for (unsigned bank = 0; bank < 7; bank++) for (unsigned c = 0; c < 2; c++) {
        struct CupidGBA* gba = machine(NULL, anchors[c]);
        prepare(gba, (group & 1) != 0, bank, group >> 1, bank % 3);
        const uint8_t* rom = gba->memory.rom;
        const uint8_t* firmware = gba->memory.bios;
        uint64_t clocks = complete(gba);
        CHECK(clocks > 1000000 && clocks < 1500000);
        CHECK(gba->memory.rom == rom && gba->memory.bios == firmware);
        CHECK(cupid_timing_global_time(&gba->timing) > anchors[c]);
        CHECK(gba->video.vcount != 0x7E);
        cupid_gba_step(gba);
        CHECK(gba->cpu.gprs[8] == 0xA1);
        observations++;
        destroy(gba);
    }
    CHECK(observations == 14);
}

struct Transfer {
    struct CupidGBA* gba;
    struct CupidTimingEvent event;
    uint64_t accepted;
    uint32_t late;
    unsigned count;
};

static void request_transfer(struct CupidTiming* timing, void* context, uint32_t late) {
    struct Transfer* transfer = context;
    struct CupidGBA* gba = transfer->gba;
    transfer->accepted = cupid_timing_global_time(timing);
    transfer->late = late;
    transfer->count++;
    CHECK(!gba->ime);
    cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM + 0x1000);
    cupid_dma_write_dad(gba, 3, GBA_BASE_EWRAM + 0x10);
    cupid_dma_write_cnt_lo(gba, 3, 4);
    cupid_dma_write_cnt(gba, 3, 0xC400);
}

struct Progress { uint64_t clocks; uint16_t timer, flags; uint32_t late; };

static struct Progress chronology(const char* bios, uint64_t clock, bool thumb, bool late) {
    struct CupidGBA* gba = machine(bios, clock);
    prepare(gba, thumb, 6, 0, 0);
    memset(gba->memory.iwram + 0x1000, 0xB6, 16);
    cupid_io_write(gba, REG_TM0CNT_H, 0);
    cupid_timing_tick(&gba->timing, 2);
    cupid_io_write(gba, REG_TM0CNT_L, 0xFFC0);
    cupid_io_write(gba, REG_TM0CNT_H, 0xC0);
    cupid_io_write(gba, REG_IE, IRQ_TIMER0 | IRQ_DMA3);
    cupid_io_write(gba, REG_IME, 1);
    cupid_timing_tick(&gba->timing, 2);
    uint64_t before = cupid_timing_global_time(&gba->timing);
    struct Transfer transfer = {.gba = gba};
    transfer.event = (struct CupidTimingEvent){.context = &transfer, .callback = request_transfer,
                                              .name = "Reset DMA request", .priority = 0};
    cupid_timing_schedule(&gba->timing, &transfer.event, 90);
    bool delayed = false;
    unsigned steps;
    for (steps = 0; steps < 2000 && !startup_entry(gba); steps++) {
        cupid_gba_step(gba);
        CHECK(!gba->cpu.halted && !gba->stopped);
        CHECK((uint32_t)gba->cpu.gprs[ARM_PC] < GBA_BASE_EWRAM);
        if (late && !delayed && cupid_timing_global_time(&gba->timing) - before >= 40) {
            /* Exercise scheduler lateness while the CPU is paused after IME
             * clears. The original event clock must survive this elapsed time. */
            CHECK(!gba->ime);
            cupid_timing_tick(&gba->timing, 317);
            delayed = true;
        }
    }
    CHECK(steps < 2000 && startup_entry(gba));
    CHECK(transfer.count == 1 && transfer.accepted == before + 90);
    CHECK(late ? transfer.late >= 250 : transfer.late < 8);
    CHECK(!gba->memory.dmaRunning && !gba->ime && !gba->irq.line);
    CHECK((gba->irqf & (IRQ_TIMER0 | IRQ_DMA3)) == (IRQ_TIMER0 | IRQ_DMA3));
    for (unsigned i = 0; i < 16; i++) CHECK(gba->memory.ewram[0x10 + i] == 0xB6);
    struct Progress result = {cupid_timing_global_time(&gba->timing) - before,
        cupid_timer_read_counter(gba, 0), gba->irqf, transfer.late};
    destroy(gba);
    return result;
}

static void chronology_matrix(void) {
    const uint64_t clocks[] = {0, INT32_MAX - 100u, UINT32_MAX - 100u, UINT64_C(0x300000000)};
    for (unsigned c = 0; c < 4; c++) for (unsigned thumb = 0; thumb < 2; thumb++)
        for (unsigned late = 0; late < 2; late++) {
            struct Progress built = chronology(NULL, clocks[c], thumb != 0, late != 0);
            if (externalBios) {
                struct Progress firmware = chronology(externalBios, clocks[c], thumb != 0, late != 0);
                CHECK(built.clocks == firmware.clocks && built.timer == firmware.timer &&
                      built.flags == firmware.flags && built.late == firmware.late);
            }
            observations++;
        }
    CHECK(observations == 16);
}

static void repeated_reboot_retains_ownership_and_elapsed_clocks(void) {
    struct CupidGBA* gba = machine(NULL, UINT32_MAX - 100u);
    uint8_t* rom = gba->memory.rom;
    uint8_t* firmware = gba->memory.bios;
    uint8_t* save = gba->memory.savedata.data;
    uint64_t duration[2][3] = {{0}};
    for (unsigned pass = 0; pass < 2; pass++) for (unsigned thumb = 0; thumb < 2; thumb++)
        for (unsigned region = 0; region < 3; region++) {
            memset(gba->memory.ewram, 0x3C, GBA_SIZE_EWRAM);
            memset(gba->memory.iwram, 0x5A, GBA_SIZE_IWRAM);
            prepare(gba, thumb != 0, 6, 3, region);
            uint64_t before = cupid_timing_global_time(&gba->timing);
            uint64_t elapsed = complete(gba);
            CHECK(cupid_timing_global_time(&gba->timing) == before + elapsed);
            CHECK(gba->memory.rom == rom && gba->memory.bios == firmware && gba->memory.savedata.data == save);
            if (pass) CHECK(duration[thumb][region] == elapsed);
            else duration[thumb][region] = elapsed;
            observations++;
        }
    CHECK(observations == 12);
    destroy(gba);
}

static void cpu_post_flag_strobes_preserve_the_neighboring_power_register(void) {
    struct CupidGBA* gba = machine(NULL, 0);
    const uint32_t arm[] = {0xE5C10000, 0xE3A020CC, 0xEAFFFFFE};
    const uint16_t narrow[] = {0x7008, 0x22CC, 0xE7FE};
    for (unsigned thumb = 0; thumb < 2; thumb++) for (unsigned old = 0; old < 2; old++)
        for (unsigned value = 0; value < 256; value++) {
            memcpy(gba->memory.bios + 0x3800, thumb ? (const void*)narrow : (const void*)arm,
                   thumb ? sizeof(narrow) : sizeof(arm));
            gba->memory.io[REG_POSTFLG >> 1] = (uint16_t)old;
            gba->cpu.gprs[0] = (int32_t)value;
            gba->cpu.gprs[1] = GBA_BASE_IO + REG_POSTFLG;
            gba->cpu.gprs[2] = 0;
            gba->cpu.gprs[ARM_PC] = 0x3800;
            _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
            if (thumb) CupidThumbWritePC(&gba->cpu); else CupidARMWritePC(&gba->cpu);
            cupid_gba_step(gba);
            CHECK(gba->memory.io[REG_POSTFLG >> 1] == (old | (value & 1)));
            CHECK(!gba->cpu.halted && !gba->stopped);
            cupid_gba_step(gba);
            CHECK(gba->cpu.gprs[2] == 0xCC);
            observations++;
        }
    CHECK(observations == 1024);
    destroy(gba);
}

static void temporary_startup_interrupt_records_only_enabled_requests(void) {
    const unsigned extra[] = {IRQ_TIMER1, IRQ_KEYPAD, IRQ_TIMER1 | IRQ_DMA3};
    for (unsigned c = 0; c < 2; c++) for (unsigned thumb = 0; thumb < 2; thumb++)
        for (unsigned combination = 0; combination < 3; combination++) {
            struct CupidGBA* gba = machine(NULL, anchors[c]);
            prepare(gba, false, 6, 3, 0);
            unsigned steps;
            for (steps = 0; steps < 2000 && !startup_entry(gba); steps++) cupid_gba_step(gba);
            CHECK(steps < 2000 && startup_entry(gba));
            const uint32_t arm = 0xEAFFFFFE;
            const uint16_t narrow = 0xE7FE;
            memcpy(gba->memory.iwram + 0x400, thumb ? (const void*)&narrow : (const void*)&arm,
                   thumb ? sizeof(narrow) : sizeof(arm));
            uint32_t registers[13];
            for (unsigned r = 0; r < 13; r++) gba->cpu.gprs[r] = (int32_t)(registers[r] = 0x22330000u + r);
            gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x400;
            _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
            if (thumb) CupidThumbWritePC(&gba->cpu); else CupidARMWritePC(&gba->cpu);
            gba->memory.iwram[0x7FF8] = IRQ_SIO;
            cupid_io_write(gba, REG_IE, IRQ_TIMER0);
            cupid_io_write(gba, REG_IME, 1);
            cupid_raise_irq(gba, IRQ_TIMER0 | extra[combination], 0);
            bool entered = false;
            for (steps = 0; steps < 200; steps++) {
                cupid_gba_step(gba);
                if (gba->cpu.privilegeMode == MODE_IRQ) entered = true;
                if (entered && gba->cpu.privilegeMode == MODE_SYS) break;
            }
            CHECK(steps < 200 && entered);
            CHECK(gba->irqf == extra[combination]);
            CHECK((word(gba->memory.iwram + 0x7FF8) & 0xFFFF) == (IRQ_SIO | IRQ_TIMER0));
            for (unsigned r = 0; r < 13; r++) CHECK((uint32_t)gba->cpu.gprs[r] == registers[r]);
            CHECK(gba->cpu.executionMode == (thumb ? MODE_THUMB : MODE_ARM));
            CHECK(!gba->cpu.halted && !gba->stopped);
            observations++;
            destroy(gba);
        }
    CHECK(observations == 12);
}

int main(int argc, char** argv) {
    unsigned selection = 0;
    unsigned group = 0;
    for (int a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--external-bios") && a + 1 < argc) externalBios = argv[++a];
        else if (!strcmp(argv[a], "--prefix") && a + 1 < argc) selection = (unsigned)strtoul(argv[++a], NULL, 0);
        else if (!strcmp(argv[a], "--complete") && a + 1 < argc) { group = 1; selection = (unsigned)strtoul(argv[++a], NULL, 0); }
        else if (!strcmp(argv[a], "--chronology")) group = 2;
        else if (!strcmp(argv[a], "--repeat")) group = 3;
        else if (!strcmp(argv[a], "--post-byte")) group = 4;
        else if (!strcmp(argv[a], "--startup-irq")) group = 5;
        else return 2;
    }
    if (group == 1) { CHECK(selection < 8 && !externalBios); complete_matrix(selection); }
    else if (group == 2) chronology_matrix();
    else if (group == 3) { CHECK(!externalBios); repeated_reboot_retains_ownership_and_elapsed_clocks(); }
    else if (group == 4) { CHECK(!externalBios); cpu_post_flag_strobes_preserve_the_neighboring_power_register(); }
    else if (group == 5) { CHECK(!externalBios); temporary_startup_interrupt_records_only_enabled_requests(); }
    else { CHECK(selection < 4); prefix_matrix(selection); }
    printf("Passed %u hard reset observations\n", observations);
    return 0;
}
