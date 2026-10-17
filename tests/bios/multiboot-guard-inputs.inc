/* SPDX-License-Identifier: GPL-3.0-or-later */
static void accumulate_guard_field(uint64_t* signature, uint64_t value) {
    for (unsigned byte = 0; byte < 8; byte++)
        *signature = (*signature ^ (uint8_t)(value >> (byte * 8))) * UINT64_C(1099511628211);
}
static void guarded_source_matrix(unsigned batch) {
    static const uint64_t clocksExpected[] = {89616, 73156, 73156};
    static const uint64_t signatures[] = {
        UINT64_C(0x90D28FB3018486E5), UINT64_C(0x2284D03DB0F327C5), UINT64_C(0x76B255E191064305)
    };
    static const struct { uint32_t source, length; } inputs[] = {
        {0x02010000, 0}, {0x02010000, 1}, {0x02010000, 7}, {0x02010000, 8},
        {0x02010000, 15}, {0x02010000, 16}, {0x02010000, 0x3FF40}, {0x02010000, 0x3FFFF},
        {0x02010000, 0x40000}, {0x02010000, 0x40008}, {0x02010000, 0x80000}, {0x02010000, 0x80008},
        {0, 256}, {0x01000000, 256}, {0x0FFFFFF8, 8}, {0xFFFFFF80, 256},
        {0x08001000, 256}, {0x0203FFF8, 16}, {0x03007FF8, 16}, {0x02000000, UINT32_MAX}
    };
    const uint32_t modes[] = {0, 1, 2, 3, INT32_MAX, UINT32_MAX};
    uint64_t signature = UINT64_C(14695981039346656037), total = 0;
    unsigned observations = 0;
    for (unsigned selected = batch * 2; selected < batch * 2 + 2; selected++)
      for (unsigned region = 0; region < 4; region++) for (unsigned thumb = 0; thumb < 2; thumb++)
      for (unsigned item = 0; item < sizeof(inputs) / sizeof(*inputs); item++) {
        struct CupidGBA* gba = machine(item & 1 ? UINT32_MAX - 16u : UINT64_C(0x300000000), (region & 1) != 0);
        if (region >= 2) parameterAddress += region & 1 ? 0x8000 : 0x40000;
        uint8_t* params = parameters(gba);
        params[20] = 0x51; params[25] = 0x42; params[26] = params[27] = 0xFF;
        params[28] = 0x93; params[30] = 2;
        put(params, 32, inputs[item].source); put(params, 36, inputs[item].source + inputs[item].length);
        uint8_t expected[128]; memcpy(expected, params, sizeof(expected));
        for (unsigned field = 0; field < 5; field++) put(expected, field * 4, 1);
        for (unsigned field = 0; field < 3; field++) put(expected, 56 + field * 4, 1);
        /* A deliberately unready peer bounds source/mode edge observations
         * before any malformed-length or unavailable-source payload is read. */
        cupid_io_write(gba, REG_RCNT, 0); cupid_io_write(gba, REG_SIOCNT, 0x1000);
        cupid_io_write(gba, REG_SIODATA32_LO, 0x6451); cupid_io_write(gba, REG_SIODATA32_HI, 0x7213);
        if (modes[selected] == 1) cupid_io_write(gba, REG_SIOCNT, 0x2000);
        const uint64_t clocks = call(gba, modes[selected], thumb != 0, item & 2 ? MODE_IRQ : MODE_SYS, NULL);
        CHECK(gba->cpu.gprs[0] == 1 && !gba->serial.active && !memcmp(params, expected, sizeof(expected)));
        accumulate_guard_field(&signature, clocks);
        for (unsigned byte = 0; byte < sizeof(expected); byte++)
            signature = (signature ^ params[byte]) * UINT64_C(1099511628211);
        total += clocks; ++observations;
        destroy(gba);
    }
    CHECK(observations == 320 && total == clocksExpected[batch] && signature == signatures[batch]);
    printf("Guard batch %u observations=%u clocks=%" PRIu64 " signature=%016" PRIx64 "\n",
           batch, observations, total, signature);
}
static void readonly_parameter_guard(void) {
    uint64_t signature = UINT64_C(14695981039346656037), total = 0;
    for (unsigned thumb = 0; thumb < 2; thumb++) for (unsigned mode = 0; mode < 4; mode++) {
        struct CupidGBA* gba = machine(0, false);
        gba->memory.rom = malloc(0x4000); CHECK(gba->memory.rom);
        gba->memory.romSize = gba->memory.romImageSize = 0x4000; gba->memory.romMask = 0x3FFF;
        memset((void*)gba->memory.rom, 0xA5, 0x4000);
        parameterAddress = 0x08000800;
        const uint64_t clocks = call(gba, mode, thumb != 0, MODE_SYS, NULL);
        CHECK(gba->cpu.gprs[0] == 1 && !gba->serial.active);
        for (unsigned byte = 0; byte < 0x4000; byte++) CHECK(((const uint8_t*)gba->memory.rom)[byte] == 0xA5);
        accumulate_guard_field(&signature, clocks); total += clocks;
        destroy(gba);
    }
    CHECK(total == 1656 && signature == UINT64_C(0x3BF3A74A5EC5D2A5));
    printf("Readonly guard clocks=%" PRIu64 " signature=%016" PRIx64 "\n", total, signature);
}
static void enabled_dma_guard(void) {
    uint64_t signature = UINT64_C(14695981039346656037), total = 0;
    for (unsigned channel = 0; channel < 4; channel++) for (unsigned thumb = 0; thumb < 2; thumb++)
      for (unsigned mode = 0; mode < 3; mode++) {
        struct CupidGBA* gba = machine(0, thumb != 0);
        uint8_t* params = parameters(gba);
        params[20] = 0x51; params[25] = 0x42; params[26] = params[27] = 0xFF;
        params[28] = 0x93; params[30] = 2;
        put(params, 32, 0x02010000); put(params, 36, 0x02010100);
        uint8_t expected[128]; memcpy(expected, params, sizeof(expected));
        for (unsigned field = 0; field < 5; field++) put(expected, field * 4, 1);
        for (unsigned field = 0; field < 3; field++) put(expected, 56 + field * 4, 1);
        const unsigned base = REG_DMA0SAD_LO + channel * 12;
        cupid_io_write(gba, base, 0); cupid_io_write(gba, base + 2, 0x0201);
        cupid_io_write(gba, base + 4, 0); cupid_io_write(gba, base + 6, 0x0202);
        cupid_io_write(gba, base + 8, 1); cupid_io_write(gba, base + 10, 0x9000);
        const uint64_t clocks = call(gba, mode, thumb != 0, MODE_SYS, NULL);
        CHECK(gba->cpu.gprs[0] == 1 && !gba->serial.active && !memcmp(params, expected, sizeof(expected)));
        CHECK(gba->memory.io[(base + 10) >> 1] == 0x9000);
        accumulate_guard_field(&signature, clocks); total += clocks;
        destroy(gba);
    }
    CHECK(total == 5896 && signature == UINT64_C(0xBF9CEEA17743485D));
    printf("Enabled DMA guard clocks=%" PRIu64 " signature=%016" PRIx64 "\n", total, signature);
}
