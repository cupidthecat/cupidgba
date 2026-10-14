/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"
#include "caller.h"

static const char* externalBios;

static struct CupidGBA* wait_machine(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    if (externalBios) CHECK(cupid_load_bios(gba, externalBios));
    cupid_gba_reset(gba);
    cupid_test_bios_caller(gba);
    return gba;
}

static void lz77_backreferences_preserve_high_bits(void) {
    /* Two literals followed by a four-byte copy at distance two. */
    const uint8_t compressed[] = {0x10, 6, 0, 0, 0x20, 0x81, 0xF1, 0x10, 1};
    const uint8_t expected[] = {0x81, 0xF1, 0x81, 0xF1, 0x81, 0xF1};
    const uint32_t destinations[] = {GBA_BASE_EWRAM + 0x100, GBA_BASE_VRAM, GBA_BASE_EWRAM + 0x100};
    const uint8_t calls[] = {0x11, 0x12, 0x12};
    for (unsigned i = 0; i < 3; i++) {
        struct CupidGBA* gba = wait_machine();
        memcpy(gba->memory.ewram, compressed, sizeof(compressed));
        gba->cpu.gprs[0] = GBA_BASE_EWRAM;
        gba->cpu.gprs[1] = destinations[i];
        const uint32_t code[] = {UINT32_C(0xEF000000) | (uint32_t)calls[i] << 16,
                                 UINT32_C(0xEAFFFFFE)};
        memcpy(gba->memory.iwram, code, sizeof(code));
        gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
        CupidARMWritePC(&gba->cpu);
        unsigned steps;
        for (steps = 0; steps < 1000; steps++) {
            cupid_gba_step(gba);
            if (gba->cpu.executionMode == MODE_ARM &&
                (uint32_t)gba->cpu.gprs[ARM_PC] == GBA_BASE_IWRAM + 8) break;
        }
        CHECK(steps < 1000);
        const uint8_t* output = i == 1 ? (const uint8_t*)gba->video.vram : gba->memory.ewram + 0x100;
        CHECK(!memcmp(output, expected, sizeof(expected)));
        CHECK((uint32_t)gba->cpu.gprs[0] == GBA_BASE_EWRAM + sizeof(compressed));
        CHECK((uint32_t)gba->cpu.gprs[1] == destinations[i] + sizeof(expected));
        CHECK(gba->cpu.gprs[3] == 0);
        cupid_gba_destroy(gba);
        free(gba);
    }
}

static void interrupt_wait_ready_flags_and_return_state(void) {
    for (unsigned configuration = 0; configuration < 8; configuration++) {
        unsigned thumb = configuration & 1;
        unsigned masked = (configuration >> 1) & 1;
        enum CupidPrivMode mode = configuration & 4 ? MODE_IRQ : MODE_SYS;
        struct CupidGBA* gba = wait_machine();
        const uint32_t armCode[] = {0xEF040000, 0xE3A0605A, 0xEAFFFFFE};
        const uint16_t thumbCode[] = {0xDF04, 0x265A, 0xE7FE};
        if (thumb) memcpy(gba->memory.iwram, thumbCode, sizeof(thumbCode));
        else memcpy(gba->memory.iwram, armCode, sizeof(armCode));
        uint16_t flags = IRQ_VBLANK | IRQ_HBLANK;
        memcpy(gba->memory.iwram + 0x7FF8, &flags, sizeof(flags));
        gba->cpu.gprs[0] = 0;
        gba->cpu.gprs[1] = IRQ_VBLANK;
        gba->cpu.gprs[2] = 0x12345678;
        gba->cpu.gprs[3] = 0x13572468;
        gba->cpu.gprs[4] = 0x24681357;
        gba->cpu.gprs[5] = 0x24682468;
        gba->cpu.gprs[11] = 0x1234;
        gba->cpu.gprs[12] = 0x5678;
        cupid_arm_set_priv_mode(&gba->cpu, mode);
        gba->cpu.cpsr.priv = mode;
        gba->cpu.spsr.packed = 0x6000003F;
        int32_t stack = gba->cpu.gprs[ARM_SP];
        gba->cpu.cpsr.i = masked;
        _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
        gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
        if (thumb) CupidThumbWritePC(&gba->cpu);
        else CupidARMWritePC(&gba->cpu);
        unsigned steps;
        for (steps = 0; steps < 500 && gba->cpu.gprs[6] != 0x5A; steps++) cupid_gba_step(gba);
        CHECK(steps < 500);
        memcpy(&flags, gba->memory.iwram + 0x7FF8, sizeof(flags));
        CHECK(flags == IRQ_HBLANK);
        CHECK(gba->cpu.gprs[0] == IRQ_VBLANK && gba->cpu.gprs[1] == IRQ_VBLANK);
        CHECK(gba->cpu.gprs[2] == 0x12345678 && gba->cpu.gprs[3] == 0);
        CHECK(gba->cpu.gprs[4] == 0x24681357 && gba->cpu.gprs[5] == 0x24682468);
        CHECK(gba->cpu.gprs[11] == 0x1234 && gba->cpu.gprs[12] == 0x5678);
        CHECK(gba->cpu.gprs[ARM_SP] == stack);
        CHECK(gba->cpu.privilegeMode == mode);
        CHECK(gba->cpu.cpsr.i == masked);
        CHECK(cupid_io_read(gba, REG_IME) == 1);
        if (mode == MODE_IRQ) CHECK(gba->cpu.spsr.packed == 0x6000003F);
        CHECK(gba->cpu.executionMode == (thumb ? MODE_THUMB : MODE_ARM));
        cupid_gba_destroy(gba);
        free(gba);
    }
}

static void interrupt_wait_discards_old_flags_and_ignores_unrelated_wakeups(void) {
    for (unsigned configuration = 0; configuration < 4; configuration++) {
        unsigned vblank = configuration & 1;
        unsigned ime = configuration >> 1;
        struct CupidGBA* gba = wait_machine();
        const uint32_t code[] = {vblank ? 0xEF050000 : 0xEF040000, 0xE3A0605A, 0xEAFFFFFE};
        memcpy(gba->memory.iwram, code, sizeof(code));
        /* The IRQ handler acknowledges IF and records all requests in the BIOS mirror. */
        const uint32_t handler[] = {
            0xE3A00301, /* mov r0, #0x04000000 */
            0xE2800C02, /* add r0, r0, #0x200 */
            0xE1D010B2, /* ldrh r1, [r0, #2] */
            0xE1C010B2, /* strh r1, [r0, #2] */
            0xE59F200C, /* ldr r2, BIOS mirror address */
            0xE1D230B0, /* ldrh r3, [r2] */
            0xE1833001, /* orr r3, r3, r1 */
            0xE1C230B0, /* strh r3, [r2] */
            0xE12FFF1E, /* bx lr */
            GBA_BASE_IWRAM + 0x7FF8
        };
        memcpy(gba->memory.iwram + 0x200, handler, sizeof(handler));
        const uint32_t entry = GBA_BASE_IWRAM + 0x200;
        memcpy(gba->memory.iwram + 0x7FFC, &entry, sizeof(entry));
        uint16_t flags = IRQ_VBLANK | IRQ_HBLANK;
        memcpy(gba->memory.iwram + 0x7FF8, &flags, sizeof(flags));
        cupid_io_write(gba, REG_IE, flags);
        cupid_io_write(gba, REG_IME, (uint16_t)ime);
        gba->cpu.gprs[0] = vblank ? 0 : 1;
        gba->cpu.gprs[1] = vblank ? IRQ_HBLANK : IRQ_VBLANK;
        gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
        CupidARMWritePC(&gba->cpu);
        unsigned steps;
        for (steps = 0; steps < 500 && !gba->cpu.halted; steps++) cupid_gba_step(gba);
        CHECK(steps < 500 && gba->cpu.gprs[6] == 0);
        memcpy(&flags, gba->memory.iwram + 0x7FF8, sizeof(flags));
        CHECK(flags == IRQ_HBLANK);
        cupid_raise_irq(gba, IRQ_HBLANK, 0);
        for (steps = 0; steps < 500; steps++) cupid_gba_step(gba);
        CHECK(gba->cpu.gprs[6] == 0 && gba->cpu.halted && gba->irqf == 0);
        uint64_t requestTime = cupid_timing_global_time(&gba->timing);
        cupid_raise_irq(gba, IRQ_VBLANK, 0);
        for (steps = 0; steps < 500 && gba->cpu.gprs[6] != 0x5A; steps++) cupid_gba_step(gba);
        CHECK(steps < 500);
        memcpy(&flags, gba->memory.iwram + 0x7FF8, sizeof(flags));
        CHECK(flags == IRQ_HBLANK);
        CHECK(gba->cpu.gprs[1] == IRQ_VBLANK);
        CHECK(gba->cpu.gprs[0] == IRQ_VBLANK && gba->cpu.gprs[3] == 0);
        CHECK(gba->cpu.gprs[4] == 0);
        /* Staged IRQ entry, this handler, wait return and the caller's MOV. */
        CHECK(cupid_timing_global_time(&gba->timing) - requestTime == 98);
        CHECK(cupid_io_read(gba, REG_IME) == 1);
        CHECK(gba->cpu.privilegeMode == MODE_SYS && !gba->cpu.halted);
        cupid_gba_destroy(gba);
        free(gba);
    }
}

int main(int argc, char** argv) {
    if (argc == 3 && !strcmp(argv[1], "--external-bios-waits")) externalBios = argv[2];
    else if (argc != 1) {
        fprintf(stderr, "Usage: %s [--external-bios-waits FILE]\n", argv[0]);
        return 2;
    }
    RUN(lz77_backreferences_preserve_high_bits);
    RUN(interrupt_wait_ready_flags_and_return_state);
    RUN(interrupt_wait_discards_old_flags_and_ignores_unrelated_wakeups);
    return 0;
}
