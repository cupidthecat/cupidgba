/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

/* Each sequence issues LDM^ and STM through the ordinary CPU entry point.
 * RAM retains both complete words; the port scene observes their data,
 * direction and control strobes through the cartridge interface. */
static void sequence(struct CupidGBA* gba, unsigned mode, unsigned first,
                     uint64_t epoch, uint16_t waitcnt, unsigned addressing,
                     bool writeback, unsigned privileged, unsigned user, bool ports,
                     unsigned window) {
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = epoch;
    gba->timing.masterCycles = (uint32_t)epoch;
    gba->audio.lastSample = gba->audio.sampleOrigin = epoch;
    gba->audio.nextSample = epoch + 512;
    cupid_arm_set_priv_mode(&gba->cpu, mode);
    gba->cpu.cpsr.packed = mode | 0xC0;
    gba->cpu.irqDisableLatch = true;
    cupid_io_write(gba, REG_WAITCNT, waitcnt);
    const unsigned second = first + 1, mask = (1u << first) | (1u << second);
    const uint32_t input[] = {0x000E000E, UINT32_C(0x5A5A0000) | user};
    const uint32_t expectedSecond = UINT32_C(0xA5A50000) | privileged;
    memcpy(gba->memory.iwram + 0x2000, input, sizeof(input));
    memset(gba->memory.iwram + 0x3000, 0, 8);
    const uint32_t start = ports ? GBA_BASE_ROM0 + (window << 25) + 0xC4 : GBA_BASE_IWRAM + 0x3000;
    const bool before = (addressing & 2) != 0, up = (addressing & 1) != 0;
    const uint32_t base = up ? start - (before ? 4u : 0u) : start + (before ? 8u : 4u);
    const uint32_t code[] = {0xE8D00000u | mask,
        0xE8010000u | (before ? 0x01000000u : 0) | (up ? 0x00800000u : 0)
            | (writeback ? 0x00200000u : 0) | mask,
        0xE1A00000, 0xE1A00000, 0xEAFFFFFE};
    memcpy(gba->memory.iwram + 0x1000, code, sizeof(code));
    gba->cpu.gprs[0] = GBA_BASE_IWRAM + 0x2000;
    gba->cpu.gprs[1] = base;
    gba->cpu.gprs[first] = 0x00010001;
    gba->cpu.gprs[second] = (int32_t)expectedSecond;
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x1000;
    CupidARMWritePC(&gba->cpu);
    cupid_gba_step(gba);
    CHECK(gba->cpu.userBankPending && gba->cpu.privilegeMode == mode);
    cupid_gba_step(gba);
    CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == GBA_BASE_IWRAM + 0x100C);
    CHECK(gba->cpu.prefetch[0] == 0xE1A00000 && gba->cpu.privilegeMode == mode);
    if (ports) {
        /* Require the transfer to cross the counter boundary: starting near
         * wrap alone would leave this part of the matrix untested. */
        if (epoch == INT32_MAX - UINT64_C(8)) CHECK(gba->timing.masterCycles > INT32_MAX);
        if (epoch == UINT32_MAX - UINT64_C(8)) CHECK(gba->timing.masterCycles < (uint32_t)epoch);
    }
    CHECK(gba->cpu.gprs[first] == 0x00010001 && (uint32_t)gba->cpu.gprs[second] == expectedSecond);
    CHECK((uint32_t)gba->cpu.gprs[1] == (writeback ? (up ? base + 8 : base - 8) : base));
    cupid_arm_set_priv_mode(&gba->cpu, MODE_SYS);
    CHECK((uint32_t)gba->cpu.gprs[first] == input[0] && (uint32_t)gba->cpu.gprs[second] == input[1]);
    cupid_arm_set_priv_mode(&gba->cpu, mode);
    if (ports) {
        CHECK(gba->cartridge.output == 15 && gba->cartridge.direction == 15);
        CHECK(cupid_memory_loadU16(&gba->cpu, start + 4, NULL) == privileged);
        CHECK(gba->cartridge.readable == (privileged != 0));
    } else {
        uint32_t words[2]; memcpy(words, gba->memory.iwram + 0x3000, sizeof(words));
        CHECK(words[0] == 0x000F000F && words[1] == expectedSecond);
    }
}

void cupid_test_user_bank_gpio(const char* directory, unsigned window) {
    CHECK(window < 3);
    char path[1024];
    int length = snprintf(path, sizeof(path), "%s/user-bank-gpio-%u.gba", directory, window);
    CHECK(length > 0 && (size_t)length < sizeof(path));
    FILE* file = fopen(path, "wb"); CHECK(file);
    const uint8_t image[256] = {0};
    CHECK(fwrite(image, 1, sizeof(image), file) == sizeof(image) && !fclose(file));
    struct CupidGBA* gba = calloc(1, sizeof(*gba)); CHECK(gba);
    cupid_gba_create(gba); CHECK(cupid_load_rom(gba, path));
    cupid_cartridge_enable_rtc(gba, true);
    const unsigned modes[] = {MODE_FIQ, MODE_IRQ, MODE_SVC, MODE_ABT, MODE_UND};
    const uint64_t epochs[] = {0, INT32_MAX - UINT64_C(8), UINT32_MAX - UINT64_C(8), UINT64_C(0x300000000)};
    unsigned pairs = 0;
    for (unsigned m = 0; m < 5; ++m) for (unsigned first = m ? 13 : 8; first < 14; ++first)
    for (unsigned e = 0; e < 4; ++e) for (unsigned prefetch = 0; prefetch < 2; ++prefetch)
    for (unsigned wait = 0; wait < 2; ++wait) for (unsigned privileged = 0; privileged < 2; ++privileged)
    for (unsigned user = 0; user < 2; ++user) for (unsigned addressing = 0; addressing < 4; ++addressing)
    for (unsigned writeback = 0; writeback < 2; ++writeback) {
        uint16_t waitcnt = (uint16_t)((prefetch << 14) | (wait ? 3u << (2 + 3 * window) : 1u << (4 + 3 * window)));
        for (unsigned ports = 0; ports < 2; ++ports)
            sequence(gba, modes[m], first, epochs[e], waitcnt, addressing, writeback != 0,
                privileged, user, ports != 0, window);
        ++pairs;
    }
    CHECK(pairs == 5120);
    cupid_gba_destroy(gba); free(gba);
    CHECK(!remove(path));
    printf("GPIO window %u: %u RAM/port scene pairs; 10240 issued LDM^/STM sequences\n", window, pairs);
}
