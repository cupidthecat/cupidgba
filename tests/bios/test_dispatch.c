/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_dispatch.c
 * @brief Unchecked SWI callback entry, nested services, interrupts and DMA.
 *
 * - Execute owned ARM callbacks reached by ARM/Thumb D4 callers.
 * - Check every NZCV and interrupt-mask combination from user/system mode.
 * - Verify stack frames, protected BIOS reads and complete caller restoration.
 */
#include "../test.h"
#include "caller.h"
#include "cupid/isa-inlines.h"

#include "dispatch-callbacks.inc"

static const char *externalBios;
static const uint32_t dmaSource[] = {0x01020304, 0x8192A3B4, 0xFFEEDDCC, 0x76543210};

static uint32_t word(const uint8_t *memory, unsigned offset) {
    uint32_t result;
    memcpy(&result, memory + offset, sizeof(result));
    return result;
}

static struct CupidGBA *machine(void) {
    struct CupidGBA *gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    if (externalBios)
        CHECK(cupid_load_bios(gba, externalBios));
    gba->memory.rom = calloc(1, 0x1000);
    CHECK(gba->memory.rom);
    gba->memory.romSize = 0x1000;
    gba->memory.romMask = 0xFFF;
    memcpy(gba->memory.rom + 0x100, dmaSource, sizeof(dmaSource));
    return gba;
}

static void guards(const struct CupidGBA *gba, unsigned offset, unsigned size) {
    for (unsigned i = 0; i < size; i++)
        CHECK(gba->memory.iwram[offset + i] == 0xA5);
}

static void scene(struct CupidGBA *gba, unsigned group, bool thumb, bool user, unsigned flags,
                  unsigned masks) {
    cupid_gba_reset(gba);
    memset(gba->memory.ewram, 0, GBA_SIZE_EWRAM);
    memset(gba->memory.iwram, 0xA5, GBA_SIZE_IWRAM);
    cupid_arm_set_priv_mode(&gba->cpu, MODE_IRQ);
    gba->cpu.gprs[ARM_SP] = 0x03007FA0;
    cupid_arm_set_priv_mode(&gba->cpu, MODE_SVC);
    gba->cpu.gprs[ARM_SP] = 0x03007FE0;
    cupid_arm_set_priv_mode(&gba->cpu, user ? MODE_USR : MODE_SYS);
    struct CupidARM *cpu = &gba->cpu;
    const uint32_t status =
        (flags << 28) | (masks << 6) | (thumb ? 0x20u : 0u) | (user ? MODE_USR : MODE_SYS);
    cpu->cpsr.packed = status;
    cpu->irqDisableLatch = (masks & 2) != 0;
    _CupidSetMode(cpu, thumb ? MODE_THUMB : MODE_ARM);
    uint32_t saved[15];
    for (unsigned r = 0; r < 15; r++)
        cpu->gprs[r] = (int32_t)(0x11000000 + r);
    cpu->gprs[0] = 0x02001000;
    cpu->gprs[ARM_SP] = 0x03007F00;
    cpu->gprs[ARM_LR] = (int32_t)0xF3F3F3F3;
    memcpy(saved, cpu->gprs, sizeof(saved));
    const uint32_t *payloads[] = {plain, nested, dma, timer};
    const size_t sizes[] = {sizeof(plain), sizeof(nested), sizeof(dma), sizeof(timer)};
    memcpy(gba->memory.ewram + 0x2040, payloads[group], sizes[group]);
    const uint32_t arm[] = {0xEFD40000, 0xEAFFFFFE, 0xEAFFFFFE};
    const uint16_t narrow[] = {0xDFD4, 0xE7FE, 0xE7FE};
    memcpy(gba->memory.iwram, thumb ? (const void *)narrow : (const void *)arm,
           thumb ? sizeof(narrow) : sizeof(arm));
    if (group == 3) {
        memcpy(gba->memory.iwram + 0x100, handler, sizeof(handler));
        const uint32_t address = 0x03000100;
        memcpy(gba->memory.iwram + 0x7FFC, &address, sizeof(address));
        cupid_io_write(gba, REG_IE, 8);
        cupid_io_write(gba, REG_IME, 1);
    }
    cpu->gprs[ARM_PC] = GBA_BASE_IWRAM;
    if (thumb)
        CupidThumbWritePC(cpu);
    else
        CupidARMWritePC(cpu);
    CHECK(cupid_test_bios_run_to_return(gba, thumb, GBA_BASE_IWRAM + (thumb ? 4 : 8), 5000));
    CHECK(cpu->privilegeMode == (user ? MODE_USR : MODE_SYS));
    CHECK((uint32_t)cpu->cpsr.packed == status);
    CHECK(cpu->irqDisableLatch == ((masks & 2) != 0));
    CHECK((uint32_t)cpu->gprs[0] == (group == 1 ? 5u : 0x11u));
    CHECK((uint32_t)cpu->gprs[1] == (group == 1 ? 2u : 0x22u));
    CHECK((uint32_t)cpu->gprs[3] == (group == 1 ? 5u : 0x33u));
    for (unsigned r = 2; r < 15; r++)
        if (r != 3)
            CHECK((uint32_t)cpu->gprs[r] == saved[r]);
    const uint32_t callbackStatus = MODE_SYS | ((masks & 2) ? 0x80u : 0u);
    for (unsigned r = 0; r < 15; r++) {
        const uint32_t expected = r == 11       ? callbackStatus
                                  : r == 12     ? 0x02002040u
                                  : r == ARM_SP ? 0x03007EF8u
                                  : r == ARM_LR ? 0x170u
                                                : saved[r];
        CHECK(word(gba->memory.ewram, 0x1000 + r * 4) == expected);
    }
    CHECK(word(gba->memory.ewram, 0x103C) == callbackStatus);
    CHECK(word(gba->memory.ewram, 0x1040) == 0xE3A0C0D3);
    CHECK(gba->memory.biosPrefetch == 0xE3A02004);
    CHECK(word(gba->memory.iwram, 0x7EF8) == saved[2]);
    CHECK(word(gba->memory.iwram, 0x7EFC) == saved[ARM_LR]);
    CHECK(word(gba->memory.iwram, 0x7FD0) == status);
    CHECK(word(gba->memory.iwram, 0x7FD4) == saved[11]);
    CHECK(word(gba->memory.iwram, 0x7FD8) == saved[12]);
    CHECK(word(gba->memory.iwram, 0x7FDC) == GBA_BASE_IWRAM + (thumb ? 2u : 4u));
    guards(gba, 0x7F00, 0x20);
    guards(gba, 0x7F40, 0x10);
    guards(gba, 0x7FA0, 0x20);
    guards(gba, 0x7FE0, group == 3 ? 0x1C : 0x20);
    if (group == 1) {
        CHECK(word(gba->memory.ewram, 0x1300) == 5);
        CHECK(word(gba->memory.ewram, 0x1304) == 2);
        CHECK(word(gba->memory.ewram, 0x1308) == saved[2]);
        CHECK(word(gba->memory.ewram, 0x130C) == 5);
    } else if (group == 2) {
        CHECK(!memcmp(gba->memory.ewram + 0x1400, dmaSource, sizeof(dmaSource)));
        CHECK(word(gba->memory.ewram, 0x13FC) == 0);
        CHECK(word(gba->memory.ewram, 0x1410) == 0);
        CHECK(!(cupid_io_read(gba, REG_DMA3CNT_HI) & 0x8000));
    } else if (group == 3) {
        CHECK(word(gba->memory.ewram, 0x1200) == ((masks & 2) ? 0u : 1u));
        CHECK((cupid_io_read(gba, REG_IF) & 8) == ((masks & 2) ? 8 : 0));
        CHECK((cupid_io_read(gba, REG_TM0CNT_H) & 0x80) == ((masks & 2) ? 0x80 : 0));
    }
}

int main(int argc, char **argv) {
    CHECK(argc == 2 || argc == 4);
    if (argc == 4) {
        CHECK(!strcmp(argv[2], "--external-bios"));
        externalBios = argv[3];
    }
    const char *groups[] = {"abi", "nested", "dma", "timer"};
    unsigned group = 0;
    while (group < 4 && strcmp(argv[1], groups[group]))
        group++;
    CHECK(group < 4);
    struct CupidGBA *gba = machine();
    unsigned count = 0;
    for (unsigned thumb = 0; thumb < 2; thumb++)
        for (unsigned user = 0; user < 2; user++)
            for (unsigned flags = 0; flags < 16; flags++)
                for (unsigned masks = 0; masks < 4; masks++) {
                    scene(gba, group, thumb != 0, user != 0, flags, masks);
                    count++;
                }
    CHECK(count == 256);
    cupid_gba_destroy(gba);
    free(gba);
    printf("PASS unchecked SWI callback %s: %u caller scenes\n", groups[group], count);
    return 0;
}
