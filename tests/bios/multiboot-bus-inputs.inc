/* SPDX-License-Identifier: GPL-3.0-or-later */
struct DownloadBusWatch {
    struct CupidGBA* gba;
    struct CupidTimingEvent dma, cleanup;
    uint64_t start, injected, workClear, firstClear, lastClear;
};
static void inject_download_dma(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct DownloadBusWatch* watch = context;
    struct CupidGBA* gba = watch->gba;
    CHECK(gba->cpu.cpsr.i && gba->cpu.privilegeMode == MODE_SYS && !gba->serial.active);
    CHECK(word(parameters(gba), 0) == 0xFFFF4293 && word(parameters(gba), 16) == 0x02010000);
    watch->injected = cupid_timing_global_time(timing) - watch->start;
    cupid_io_write(gba, REG_DMA0SAD_LO, 0x9000); cupid_io_write(gba, REG_DMA0SAD_HI, 0x0201);
    cupid_io_write(gba, REG_DMA0DAD_LO, 0x0080); cupid_io_write(gba, REG_DMA0DAD_HI, 0x0201);
    cupid_io_write(gba, REG_DMA0CNT_LO, 1); cupid_io_write(gba, REG_DMA0CNT_HI, 0xC400);
}
static void observe_download_cleanup(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct DownloadBusWatch* watch = context;
    const uint8_t* params = parameters(watch->gba);
    const uint64_t relative = cupid_timing_global_time(timing) - watch->start;
    if (!watch->workClear && !word(params, 56)) watch->workClear = relative;
    if (!watch->firstClear && !word(params, 0)) watch->firstClear = relative;
    if (!watch->lastClear && !word(params, 16)) watch->lastClear = relative;
    if (!watch->lastClear) cupid_timing_schedule(timing, &watch->cleanup, 1);
}
static void watch_download_completion(void* context, uint64_t timestamp) {
    struct DownloadBusWatch* watch = context;
    CHECK(timestamp >= watch->start && !watch->workClear && !watch->firstClear && !watch->lastClear);
    cupid_timing_schedule(&watch->gba->timing, &watch->cleanup, 1);
}
static void start_download_bus_watch(struct DownloadBusWatch* watch) {
    struct CupidGBA* gba = watch->gba;
    const uint32_t handler[] = {0xE59F0018, 0xE3A01010, 0xE1C010B0, 0xE59F0010,
        0xE5901000, 0xE2811001, 0xE5801000, 0xE12FFF1E, 0x04000202, 0x02018000};
    memcpy(gba->memory.iwram + 0x600, handler, sizeof(handler));
    put(gba->memory.iwram, 0x7FFC, 0x03000600);
    put(gba->memory.ewram, 0x18000, 0); put(gba->memory.ewram, 0x19000, 0x89ABCDEF);
    cupid_io_write(gba, REG_IE, IRQ_TIMER1); cupid_io_write(gba, REG_IME, 1);
    cupid_io_write(gba, REG_TM1CNT_L, 0xFC00); cupid_io_write(gba, REG_TM1CNT_H, 0xC0);
    watch->start = cupid_timing_global_time(&gba->timing);
    watch->dma = (struct CupidTimingEvent){.callback = inject_download_dma, .context = watch,
        .priority = 255, .name = "Download DMA source mutation"};
    watch->cleanup = (struct CupidTimingEvent){.callback = observe_download_cleanup, .context = watch,
        .priority = 255, .name = "Download partial cleanup"};
    cupid_timing_schedule(&gba->timing, &watch->dma, 500);
}
static void finish_download_bus_watch(struct DownloadBusWatch* watch, bool thumb, unsigned mode) {
    struct CupidGBA* gba = watch->gba;
    static const uint64_t chronology[][3] = {{1238352, 1238375, 1238419}, {1206223, 1206246, 1206290}};
    CHECK(watch->injected == 500 && watch->workClear && watch->firstClear && watch->lastClear);
    CHECK(watch->injected < watch->workClear && watch->workClear < watch->firstClear && watch->firstClear < watch->lastClear);
    CHECK(watch->workClear == chronology[thumb][0] && watch->firstClear == chronology[thumb][1] &&
          watch->lastClear == chronology[thumb][2]);
    CHECK(word(gba->memory.ewram, 0x10080) == 0x89ABCDEF);
    CHECK((gba->memory.io[REG_IF >> 1] & (IRQ_TIMER1 | IRQ_DMA0)) == (IRQ_TIMER1 | IRQ_DMA0));
    CHECK(!word(gba->memory.ewram, 0x18000) && !gba->cpu.cpsr.i);
    unsigned steps;
    for (steps = 0; steps < 2000; steps++) {
        cupid_gba_step(gba);
        if (word(gba->memory.ewram, 0x18000) && gba->cpu.privilegeMode == MODE_SYS &&
            gba->cpu.executionMode == (thumb ? MODE_THUMB : MODE_ARM) &&
            (uint32_t)gba->cpu.gprs[ARM_PC] == 0x03000100u + (thumb ? 4u : 8u)) break;
    }
    CHECK(steps < 2000 && gba->cpu.gprs[0] == 0 && (unsigned)gba->cpu.gprs[1] == mode);
    CHECK((uint32_t)gba->cpu.cpsr.packed == (0xA000001Fu | (thumb ? 32u : 0u)));
    for (unsigned reg = 2; reg < 15; reg++)
        CHECK((uint32_t)gba->cpu.gprs[reg] == (reg == ARM_SP ? 0x03007000u : 0x11220000u + reg));
    cupid_timing_deschedule(&gba->timing, &watch->dma); cupid_timing_deschedule(&gba->timing, &watch->cleanup);
    printf("Bus chronology injected=%" PRIu64 " work=%" PRIu64 " first=%" PRIu64 " last=%" PRIu64 "\n",
        watch->injected, watch->workClear, watch->firstClear, watch->lastClear);
}
