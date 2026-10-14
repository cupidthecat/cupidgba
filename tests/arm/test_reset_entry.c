/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"
#include <string.h>

static const enum CupidPrivMode modes[] = {MODE_USR, MODE_FIQ, MODE_IRQ,
    MODE_SVC, MODE_ABT, MODE_UND, MODE_SYS};

static struct CupidGBA* machine(bool external) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    gba->memory.rom = calloc(1, 0x4000);
    CHECK(gba->memory.rom);
    gba->memory.romSize = gba->memory.romImageSize = 0x4000;
    gba->memory.romMask = 0x3FFF;
    const uint32_t code[] = {0xE10F0000, 0xEAFFFFFE}; /* MRS r0,CPSR; B . */
    memcpy(gba->memory.rom, code, sizeof(code));
    if (external) {
        memcpy(gba->memory.bios, code, sizeof(code));
        gba->memory.fullBios = 1;
        gba->memory.biosLoaded = true;
        gba->hasBios = true;
        gba->biosHLE = false;
    }
    cupid_savedata_force_type(&gba->memory.savedata, SAVE_SRAM);
    gba->memory.savedata.data[9] = 0xA5;
    gba->memory.savedata.dirty = true;
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void prior_state(struct CupidGBA* gba, enum CupidPrivMode mode,
                        unsigned thumb, unsigned masks, bool latch) {
    cupid_arm_set_priv_mode(&gba->cpu, mode);
    gba->cpu.cpsr.packed = 0xA0000000 | (uint32_t)mode | (masks << 6);
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.irqDisableLatch = latch;
    gba->cpu.gprs[ARM_SP] = 0x03000100;
    gba->cpu.gprs[ARM_LR] = 0x08000203;
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x102;
    gba->cpu.prefetch[0] = 0x11223344;
    gba->cpu.prefetch[1] = 0xA55A5AA5;
    gba->cpu.spsr.packed = 0xF000003F;
}

static void check_entry(struct CupidGBA* gba, bool external) {
    CHECK((gba->cpu.cpsr.packed & 255) == (external ? 0xD3 : 0x1F));
    CHECK(gba->cpu.privilegeMode == (external ? MODE_SVC : MODE_SYS));
    CHECK(gba->cpu.executionMode == MODE_ARM && !gba->cpu.cpsr.t);
    CHECK(gba->cpu.irqDisableLatch == external);
    CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == (external ? 4 : GBA_BASE_ROM0 + 4));
    CHECK(gba->cpu.prefetch[0] == 0xE10F0000 && gba->cpu.prefetch[1] == 0xEAFFFFFE);
    CHECK((const void*)gba->cpu.memory.activeRegion == (external ? gba->memory.bios : gba->memory.rom));
    CHECK(gba->cpu.memory.activeMask == 0x3FFC);
    CHECK(gba->biosHLE == !external && gba->memory.fullBios == external);
    CHECK(gba->memory.savedata.data[9] == 0xA5 && gba->memory.savedata.dirty);
}

static void external_reset_control_and_first_instruction(void) {
    struct CupidGBA* gba = machine(true);
    for (unsigned m = 0; m < sizeof(modes) / sizeof(*modes); m++) {
        for (unsigned thumb = 0; thumb < 2; thumb++) {
            for (unsigned masks = 0; masks < 4; masks++) {
                for (unsigned latch = 0; latch < 2; latch++) {
                    prior_state(gba, modes[m], thumb, masks, latch != 0);
                    cupid_gba_reset(gba);
                    check_entry(gba, true);
                    cupid_gba_step(gba);
                    CHECK((gba->cpu.gprs[0] & 255) == 0xD3);
                }
            }
        }
    }
    /* The CPU reset primitive itself also has the architectural entry. */
    void (*callback)(struct CupidARM*) = gba->cpu.irqh.reset;
    gba->cpu.irqh.reset = NULL;
    cupid_arm_reset(&gba->cpu);
    CHECK((gba->cpu.cpsr.packed & 255) == 0xD3 && gba->cpu.privilegeMode == MODE_SVC);
    CHECK(gba->cpu.irqDisableLatch && gba->cpu.gprs[ARM_PC] == 4);
    gba->cpu.irqh.reset = callback;
    destroy(gba);
}

static void fast_entry_retains_post_boot_control_and_stacks(void) {
    struct CupidGBA* gba = machine(false);
    for (unsigned m = 0; m < sizeof(modes) / sizeof(*modes); m++) {
        for (unsigned thumb = 0; thumb < 2; thumb++) {
            for (unsigned masks = 0; masks < 4; masks++) {
                for (unsigned latch = 0; latch < 2; latch++) {
                    prior_state(gba, modes[m], thumb, masks, latch != 0);
                    cupid_gba_reset(gba);
                    check_entry(gba, false);
                    CHECK(gba->cpu.gprs[ARM_SP] == GBA_SP_BASE_SYSTEM);
                    CHECK(cupid_io_read(gba, REG_POSTFLG) == 1);
                    CHECK(cupid_io_read(gba, REG_VCOUNT) == 0x7E);
                    cupid_gba_step(gba);
                    CHECK((gba->cpu.gprs[0] & 255) == 0x1F);
                    cupid_arm_set_priv_mode(&gba->cpu, MODE_IRQ);
                    CHECK(gba->cpu.gprs[ARM_SP] == GBA_SP_BASE_IRQ);
                    cupid_arm_set_priv_mode(&gba->cpu, MODE_SVC);
                    CHECK(gba->cpu.gprs[ARM_SP] == GBA_SP_BASE_SUPERVISOR);
                    cupid_arm_set_priv_mode(&gba->cpu, MODE_SYS);
                }
            }
        }
    }
    destroy(gba);
}

static void pending_irq_waits_for_firmware_to_unmask(void) {
    struct CupidGBA* gba = machine(true);
    const uint32_t code[] = {0xE10F0000, 0xE321F01F, 0xE3A01001, 0xEAFFFFFE};
    const uint32_t handler[] = {0xE14F2000, 0xEAFFFFFE}; /* MRS r2,SPSR; B . */
    memcpy(gba->memory.bios, code, sizeof(code));
    memcpy(gba->memory.bios + BASE_IRQ, handler, sizeof(handler));
    cupid_gba_reset(gba);
    cupid_io_write(gba, REG_IE, IRQ_TIMER0);
    cupid_io_write(gba, REG_IME, 1);
    cupid_raise_irq(gba, IRQ_TIMER0, cupid_timing_global_time(&gba->timing));
    cupid_timing_tick(&gba->timing, 3);
    CHECK(gba->irq.line);
    cupid_gba_step(gba);
    CHECK((gba->cpu.gprs[0] & 255) == 0xD3 && gba->cpu.privilegeMode == MODE_SVC);
    for (unsigned i = 0; i < 32 && gba->cpu.gprs[2] != 0x1F; i++) cupid_gba_step(gba);
    CHECK(gba->cpu.gprs[2] == 0x1F && gba->cpu.privilegeMode == MODE_IRQ);
    destroy(gba);
}

static void repeated_resets_keep_image_and_save_ownership(void) {
    for (unsigned external = 0; external < 2; external++) {
        struct CupidGBA* gba = machine(external != 0);
        uint8_t* image = gba->memory.bios;
        uint8_t* rom = gba->memory.rom;
        uint8_t* save = gba->memory.savedata.data;
        uint8_t firmware[GBA_SIZE_BIOS];
        memcpy(firmware, image, sizeof(firmware));
        for (unsigned i = 0; i < 16; i++) {
            prior_state(gba, modes[i % 7], i & 1, i % 4, (i & 1) != 0);
            cupid_gba_reset(gba);
            check_entry(gba, external != 0);
            CHECK(gba->memory.bios == image && gba->memory.rom == rom && gba->memory.savedata.data == save);
            CHECK(gba->memory.romImageSize == 0x4000);
            if (external) CHECK(!memcmp(firmware, image, sizeof(firmware)));
            uint32_t pc = (uint32_t)gba->cpu.gprs[ARM_PC];
            cupid_gba_step(gba);
            CHECK((uint32_t)gba->cpu.gprs[ARM_PC] != pc);
            /* A skip request after execution has begun leaves the CPU alone. */
            uint32_t cpsr = gba->cpu.cpsr.packed;
            pc = (uint32_t)gba->cpu.gprs[ARM_PC];
            cupid_gba_skip_bios(gba);
            CHECK((uint32_t)gba->cpu.cpsr.packed == cpsr && (uint32_t)gba->cpu.gprs[ARM_PC] == pc);
        }
        destroy(gba);
    }
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "external")) RUN(external_reset_control_and_first_instruction);
    else if (!strcmp(argv[1], "fast")) RUN(fast_entry_retains_post_boot_control_and_stacks);
    else if (!strcmp(argv[1], "irq")) RUN(pending_irq_waits_for_firmware_to_unmask);
    else if (!strcmp(argv[1], "repeat")) RUN(repeated_resets_keep_image_and_save_ownership);
    else CHECK(false);
    return 0;
}
