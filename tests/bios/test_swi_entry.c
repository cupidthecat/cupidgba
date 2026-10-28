/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

static const char* externalBios;
static const enum CupidPrivMode modes[] = {MODE_USR, MODE_SYS, MODE_FIQ, MODE_IRQ, MODE_SVC, MODE_ABT, MODE_UND};
static const uint64_t epochs[] = {0x1000, UINT64_C(0x7FFFFFFE), UINT64_C(0xFFFFFFFE), UINT64_C(0x2FFFFFFFE)};

struct Issue { unsigned count; uint32_t address, opcode; enum CupidExecMode mode; };
static void observe(const struct CupidARM* cpu, void* context) {
    struct Issue* issue = context;
    issue->count++;
    CHECK((uint32_t)cpu->gprs[ARM_PC] == issue->address + (issue->mode == MODE_THUMB ? 2u : 4u));
    CHECK(cpu->executionMode == issue->mode && cpu->prefetch[0] == issue->opcode);
}

static struct CupidGBA* machine(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba)); CHECK(gba);
    cupid_gba_create(gba);
    if (externalBios) CHECK(cupid_load_bios(gba, externalBios));
    gba->memory.rom = calloc(1, GBA_SIZE_ROM); CHECK(gba->memory.rom);
    gba->memory.romSize = GBA_SIZE_ROM;
    gba->memory.romMask = GBA_SIZE_ROM - 1;
    return gba;
}
static void destroy(struct CupidGBA* gba) { cupid_gba_destroy(gba); free(gba); }

static unsigned bank(enum CupidPrivMode mode) {
    switch (mode) {
    case MODE_FIQ: return BANK_FIQ;
    case MODE_IRQ: return BANK_IRQ;
    case MODE_SVC: return BANK_SUPERVISOR;
    case MODE_ABT: return BANK_ABORT;
    case MODE_UND: return BANK_UNDEFINED;
    default: return BANK_NONE;
    }
}

/* Own the caller stream and stop after its single decoded instruction. No
 * firmware boot or service body is executed, including for undefined IDs. */
static uint64_t scene(struct CupidGBA* gba, bool thumb, unsigned service, uint16_t low,
                      enum CupidPrivMode mode, unsigned flags, unsigned masks,
                      uint32_t address, uint16_t waits, uint64_t epoch,
                      unsigned condition, bool taken) {
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = epoch;
    gba->timing.masterCycles = (uint32_t)epoch;
    struct CupidARM* cpu = &gba->cpu;
    cupid_arm_set_priv_mode(cpu, mode);
    for (unsigned b = 0; b < 6; b++) {
        for (unsigned r = 0; r < 7; r++) cpu->bankedRegisters[b][r] = 0x51000000 + b * 0x100 + r;
        cpu->bankedSPSRs[b] = 0x62000010 + b * 0x10000;
    }
    for (unsigned r = 0; r < 15; r++) cpu->gprs[r] = 0x11000000 + r;
    cpu->spsr.packed = 0x6000003F;
    const uint32_t status = (flags << 28) | (masks << 6) | (thumb ? 0x20u : 0u) | mode;
    cpu->cpsr.packed = status;
    cpu->irqDisableLatch = (masks & 2) != 0;
    _CupidSetMode(cpu, thumb ? MODE_THUMB : MODE_ARM);
    cupid_memory_update_waitstates(gba, waits);
    uint8_t* code = address >= GBA_BASE_ROM0 ? gba->memory.rom + (address & (GBA_SIZE_ROM - 1))
                  : address >= GBA_BASE_IWRAM ? gba->memory.iwram + (address & (GBA_SIZE_IWRAM - 1))
                  : gba->memory.ewram + (address & (GBA_SIZE_EWRAM - 1));
    const uint32_t opcode = thumb ? 0xDF00u | service
        : (condition << 28) | 0x0F000000u | (service << 16) | low;
    if (thumb) {
        const uint16_t stream[] = {(uint16_t)opcode, 0x46C0, 0x46C0};
        memcpy(code, stream, sizeof(stream));
    } else {
        const uint32_t stream[] = {opcode, 0xE1A00000, 0xE1A00000};
        memcpy(code, stream, sizeof(stream));
    }
    cpu->gprs[ARM_PC] = (int32_t)address;
    if (thumb) CupidThumbWritePC(cpu); else CupidARMWritePC(cpu);
    struct Issue issue = {0, address, opcode, thumb ? MODE_THUMB : MODE_ARM};
    cpu->instructionObserver = observe;
    cpu->instructionObserverContext = &issue;
    int32_t saved[15], expectedBanks[6][7], expectedStatus[6];
    memcpy(saved, cpu->gprs, sizeof(saved));
    memcpy(expectedBanks, cpu->bankedRegisters, sizeof(expectedBanks));
    memcpy(expectedStatus, cpu->bankedSPSRs, sizeof(expectedStatus));
    const uint64_t before = cupid_timing_global_time(&gba->timing);
    cupid_gba_step(gba);
    CHECK(issue.count == 1 && !cpu->halted && !gba->stopped);
    if (!taken) {
        CHECK(cpu->privilegeMode == mode && (uint32_t)cpu->cpsr.packed == status);
        CHECK((uint32_t)cpu->spsr.packed == 0x6000003F);
        CHECK((uint32_t)cpu->gprs[ARM_PC] == address + 8);
        CHECK(!memcmp(saved, cpu->gprs, sizeof(saved)));
        CHECK(!memcmp(expectedBanks, cpu->bankedRegisters, sizeof(expectedBanks)));
        CHECK(!memcmp(expectedStatus, cpu->bankedSPSRs, sizeof(expectedStatus)));
    } else {
        if (cpu->privilegeMode != MODE_SVC) fprintf(stderr, "service=%02X thumb=%u condition=%X\n", service, thumb, condition);
        CHECK(cpu->privilegeMode == MODE_SVC && cpu->executionMode == MODE_ARM);
        CHECK((uint32_t)cpu->cpsr.packed == ((status & ~0x3Fu) | MODE_SVC | 0x80u));
        CHECK((uint32_t)cpu->spsr.packed == status && cpu->irqDisableLatch);
        CHECK(cpu->gprs[ARM_PC] == BASE_SWI + 4);
        CHECK((uint32_t)cpu->gprs[ARM_LR] == address + (thumb ? 2u : 4u));
        CHECK(cpu->gprs[ARM_SP] == (mode == MODE_SVC ? saved[ARM_SP] : expectedBanks[BANK_SUPERVISOR][0]));
        for (unsigned r = 0; r < 13; r++) {
            int32_t expected = mode == MODE_FIQ && r >= 8 ? expectedBanks[BANK_NONE][r - 6] : saved[r];
            CHECK(cpu->gprs[r] == expected);
        }
        if (mode != MODE_SVC) {
            expectedBanks[bank(mode)][0] = saved[ARM_SP];
            expectedBanks[bank(mode)][1] = saved[ARM_LR];
            expectedStatus[bank(mode)] = 0x6000003F;
            if (mode == MODE_FIQ) for (unsigned r = 8; r < 13; r++) expectedBanks[BANK_FIQ][r - 6] = saved[r];
        }
        CHECK(!memcmp(expectedBanks, cpu->bankedRegisters, sizeof(expectedBanks)));
        CHECK(!memcmp(expectedStatus, cpu->bankedSPSRs, sizeof(expectedStatus)));
        uint32_t vector[2]; memcpy(vector, gba->memory.bios + BASE_SWI, sizeof(vector));
        CHECK(cpu->prefetch[0] == vector[0] && cpu->prefetch[1] == vector[1]);
        CHECK(gba->memory.activeRegion == GBA_REGION_BIOS);
    }
    cpu->instructionObserver = NULL;
    cpu->instructionObserverContext = NULL;
    const uint64_t elapsed = cupid_timing_global_time(&gba->timing) - before;
    CHECK(cupid_timing_global_time(&gba->timing) == epoch + elapsed);
    if (address == GBA_BASE_IWRAM) CHECK(elapsed == (taken ? 3u : 1u));
    return elapsed;
}

static void services(bool thumb) {
    struct CupidGBA* gba = machine(); unsigned count = 0;
    for (unsigned service = 0; service < 256; service++) for (unsigned mask = 0; mask < 4; mask++) {
        for (unsigned e = 0; e < 4; e++) {
            scene(gba, thumb, service, 0, MODE_SYS, (service ^ (e * 5)) & 15, mask,
                  GBA_BASE_IWRAM, 0, epochs[e], 14, true);
            count++;
        }
    }
    CHECK(count == 4096); printf("PASS all 256 %s service bytes: %u entry scenes\n", thumb ? "Thumb" : "ARM", count);
    destroy(gba);
}

static void callers(void) {
    const unsigned ids[] = {6, 0xFF};
    struct CupidGBA* gba = machine(); unsigned count = 0;
    for (unsigned m = 0; m < 7; m++) for (unsigned t = 0; t < 2; t++) {
        for (unsigned flags = 0; flags < 16; flags++) for (unsigned masks = 0; masks < 4; masks++) {
            for (unsigned id = 0; id < 2; id++) {
                scene(gba, t != 0, ids[id], 0xA55A, modes[m], flags, masks,
                      GBA_BASE_IWRAM, 0, epochs[3], 14, true);
                count++;
            }
        }
    }
    CHECK(count == 1792); printf("PASS all caller banks, flags and interrupt masks: %u scenes\n", count);
    destroy(gba);
}

static bool condition_passes(unsigned condition, unsigned flags) {
    bool n = (flags & 8) != 0, z = (flags & 4) != 0;
    bool c = (flags & 2) != 0, v = (flags & 1) != 0;
    const bool result[] = {z, !z, c, !c, n, !n, v, !v, c && !z, !c || z,
                           n == v, n != v, !z && n == v, z || n != v, true, false};
    return result[condition];
}
static void predicates(void) {
    const unsigned ids[] = {0, 0x2B, 0xFF};
    const uint16_t lows[] = {0, 1, 0xA55A, 0xFFFF};
    struct CupidGBA* gba = machine(); unsigned count = 0;
    for (unsigned cond = 0; cond < 16; cond++) for (unsigned flags = 0; flags < 16; flags++) {
        for (unsigned id = 0; id < 3; id++) for (unsigned low = 0; low < 4; low++) {
            scene(gba, false, ids[id], lows[low], MODE_SYS, flags, 0, GBA_BASE_IWRAM,
                  0, epochs[2], cond, condition_passes(cond, flags));
            count++;
        }
    }
    CHECK(count == 3072); printf("PASS ARM predicates and ignored immediate bits: %u scenes\n", count);
    destroy(gba);
}

static void sources(void) {
    const uint32_t addresses[] = {GBA_BASE_EWRAM, GBA_BASE_IWRAM, 0x08000100, 0x09000100,
                                 0x0A000100, 0x0B000100, 0x0C000100, 0x0D000100};
    const unsigned nshift[] = {2, 5, 8}, sshift[] = {4, 7, 10};
    const unsigned ids[] = {0x2B, 0xF0, 0xFA, 0xFF};
    struct CupidGBA* gba = machine(); unsigned count = 0;
    for (unsigned a = 0; a < 8; a++) for (unsigned t = 0; t < 2; t++) {
        for (unsigned n = 0; n < 4; n++) for (unsigned s = 0; s < 2; s++) {
            for (unsigned p = 0; p < 2; p++) for (unsigned e = 0; e < 4; e++) {
                unsigned b = a < 2 ? 0 : (a - 2) / 2;
                uint16_t waits = (uint16_t)((n << nshift[b]) | (s << sshift[b]) | (p << 14));
                uint64_t expected = scene(gba, t != 0, 6, 0, MODE_SYS, 10, 3, addresses[a], waits, epochs[e], 14, true);
                CHECK(expected >= 3);
                count++;
                for (unsigned id = 0; id < 4; id++) {
                    CHECK(scene(gba, t != 0, ids[id], 0xFFFF, MODE_SYS, 10, 3, addresses[a], waits, epochs[e], 14, true) == expected);
                    count++;
                }
            }
        }
    }
    CHECK(count == 5120); printf("PASS caller buses, waits, prefetch and clock epochs: %u scenes\n", count);
    destroy(gba);
}

int main(int argc, char** argv) {
    CHECK(argc == 2 || (argc == 4 && !strcmp(argv[2], "--bios")));
    if (argc == 4) externalBios = argv[3];
    if (!strcmp(argv[1], "arm")) services(false);
    else if (!strcmp(argv[1], "thumb")) services(true);
    else if (!strcmp(argv[1], "banks")) callers();
    else if (!strcmp(argv[1], "predicates")) predicates();
    else if (!strcmp(argv[1], "sources")) sources();
    else CHECK(false);
    return 0;
}
