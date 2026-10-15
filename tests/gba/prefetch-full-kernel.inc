/* SPDX-License-Identifier: GPL-3.0-or-later */
static void full_queue_rom_data_flush_kernel(void) {
    const uint64_t clocks[] = {0, UINT32_MAX - 10u, UINT64_C(0x300000000)};
    for (unsigned clock = 0; clock < sizeof(clocks) / sizeof(*clocks); clock++) {
        struct CupidGBA* gba = machine(clocks[clock]);
        cupid_memory_update_waitstates(gba, 0x4014);
        /* Owned ARM kernel: fill the queue with nine RAM loads, flush it with
         * a ROM data load, then read timer zero after three ordinary opcodes. */
        uint32_t program[] = {
            0xE3A00000, 0xE5830000, 0, 0xE5940000, 0xE5830000, 0xE3A01402,
            0xE5910000, 0xE5910000, 0xE5910000, 0xE5910000, 0xE5910000,
            0xE5910000, 0xE5910000, 0xE5910000, 0xE5910000,
            0, 0xE5944000, 0xE1A00000, 0xE1A00000, 0xE1A00000,
            0xE5930000, 0xE20000FF, 0xEAFFFFFE, 0x00800000
        };
        const uint32_t base = GBA_BASE_ROM0 + 0x100;
        const uint32_t literal = base + sizeof(program) - 4;
        program[2] = 0xE28F4000 | (literal - (base + 2 * 4 + 8));
        program[15] = 0xE28F4000 | (literal - (base + 15 * 4 + 8));
        memcpy(gba->memory.rom + 0x100, program, sizeof(program));
        gba->cpu.gprs[3] = 0x04000100;
        gba->cpu.gprs[ARM_PC] = (int32_t)base;
        CupidARMWritePC(&gba->cpu);
        unsigned steps;
        for (steps = 0; steps < 10000; steps++) {
            if ((uint32_t)gba->cpu.gprs[ARM_PC] == base + 23 * 4 &&
                gba->cpu.prefetch[0] == 0xEAFFFFFE) break;
            cupid_gba_step(gba);
        }
        CHECK(steps < 10000);
        if (gba->cpu.gprs[0] != 0x6E)
            fprintf(stderr, "Full-queue ROM-data timer: observed %u, expected 110\n", (unsigned)gba->cpu.gprs[0]);
        CHECK(gba->cpu.gprs[0] == 0x6E);
        CHECK(!memcmp(gba->memory.rom + 0x100, program, sizeof(program)));
        destroy(gba);
    }
}
