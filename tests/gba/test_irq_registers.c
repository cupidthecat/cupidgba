/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../test.h"
#include <string.h>

struct History {
    struct CupidGBA* gba;
    struct CupidTimingEvent event;
    unsigned count;
    uint16_t ie[4], flags[4];
    bool ime[4], available[4], line[4];
};

static void sample(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct History* history = context;
    CHECK(history->count < 4);
    unsigned i = history->count++;
    history->ie[i] = cupid_io_read(history->gba, REG_IE);
    history->flags[i] = cupid_io_read(history->gba, REG_IF);
    history->ime[i] = cupid_io_read(history->gba, REG_IME) != 0;
    history->available[i] = history->gba->irq.available;
    history->line[i] = history->gba->irq.line;
    if (history->count < 4) cupid_timing_schedule(timing, &history->event, 1);
}

static void seed(struct CupidGBA* gba, uint64_t epoch, uint16_t ie, bool ime) {
    cupid_timing_clear(&gba->timing);
    cupid_irq_init(gba);
    gba->timing.globalCycles = epoch;
    gba->timing.masterCycles = (uint32_t)epoch;
    gba->ie = gba->irq.pendingIE = ie;
    gba->ime = gba->irq.pendingIME = ime;
    gba->irqf = 0;
    gba->memory.io[REG_IE >> 1] = ie;
    gba->memory.io[REG_IF >> 1] = 0;
    gba->memory.io[REG_IME >> 1] = ime;
}

static void write_ie(struct CupidGBA* gba, unsigned width, uint16_t value) {
    if (width == 0) cupid_io_write(gba, REG_IE, value);
    else if (width == 1) {
        cupid_io_write8(gba, REG_IE, (uint8_t)value);
        cupid_io_write8(gba, REG_IE + 1, (uint8_t)(value >> 8));
    } else {
        /* The upper half acknowledges all currently accepted IF bits. It
         * must not cancel a separately phased request that arrives later. */
        cupid_io_write32(gba, REG_IE, UINT32_C(0xFFFF0000) | value);
    }
}

static void independently_queued_registers(void) {
    const uint64_t epochs[] = {0, INT32_MAX - 1u, UINT32_MAX - 1u,
        UINT64_C(0x100000001), UINT64_C(0x300000000)};
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 5; ++epoch)
      for (unsigned bit = 0; bit < 14; ++bit)
        for (unsigned mask = 0; mask < 4; ++mask)
          for (unsigned ime = 0; ime < 2; ++ime)
            for (unsigned width = 0; width < 3; ++width)
              for (unsigned order = 0; order < 3; ++order)
                for (unsigned bulk = 0; bulk < 2; ++bulk) {
                    uint16_t source = (uint16_t)(1u << bit);
                    uint16_t initial = mask == 0 ? 0 : mask == 1 ? 0x3FFF
                        : mask == 2 ? source : (uint16_t)(0x3FFF ^ source);
                    uint16_t changed = initial ^ 0x3FFF;
                    seed(gba, epochs[epoch], initial, ime != 0);
                    uint64_t now = cupid_timing_global_time(&gba->timing);
                    /* Display requests can be announced one clock before
                     * their source phase. A later IF acceptance owns no IE
                     * or IME write, regardless of host call order. */
                    if (order == 0) cupid_raise_irq_at(gba, source, now + 1);
                    write_ie(gba, width, changed | 0xC000);
                    if (order == 1) cupid_raise_irq_at(gba, source, now + 1);
                    if (width == 1) cupid_io_write8(gba, REG_IME, (uint8_t)(ime ^ 1));
                    else cupid_io_write(gba, REG_IME, (uint16_t)(ime ^ 1));
                    if (order == 2) cupid_raise_irq_at(gba, source, now + 1);
                    struct History history = {.gba = gba};
                    history.event = (struct CupidTimingEvent){.context = &history,
                        .callback = sample, .priority = 8};
                    cupid_timing_schedule(&gba->timing, &history.event, 1);
                    if (bulk) cupid_timing_tick(&gba->timing, 4);
                    else for (unsigned clock = 0; clock < 4; ++clock)
                        cupid_timing_tick(&gba->timing, 1);
                    CHECK(history.count == 4);
                    for (unsigned i = 0; i < 4; ++i) {
                        CHECK(history.ie[i] == changed);
                        CHECK(history.ime[i] == (ime == 0));
                        CHECK(history.flags[i] == (i == 0 ? 0 : source));
                        CHECK(history.available[i] == (i >= 2 && (changed & source)));
                        CHECK(history.line[i] == (i == 3 && !ime && (changed & source)));
                    }
                    /* Reuse the freed slot for IF acknowledgement. Its old
                     * payload must not turn into another IE or IME write. */
                    cupid_io_write(gba, REG_IF, source);
                    cupid_timing_tick(&gba->timing, 3);
                    CHECK(gba->ie == changed && gba->ime == !ime && !gba->irqf);
                    CHECK(!gba->irq.available && !gba->irq.line);
                    ++scenes;
                }
    CHECK(scenes == 10080);
    printf("Independent IRQ register scenes: %u\n", scenes);
    cupid_gba_destroy(gba);
    free(gba);
}

static void timed_display_register_writes(void) {
    const uint64_t epochs[] = {0, INT32_MAX - 963u, UINT32_MAX - 963u,
        UINT64_C(0x100000001), UINT64_C(0x300000000)};
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 5; ++epoch)
      for (unsigned initial = 0; initial < 2; ++initial)
        for (unsigned width = 0; width < 3; ++width)
          for (unsigned reg = 0; reg < 2; ++reg)
            for (int phase = -4; phase <= 4; ++phase) {
                seed(gba, epochs[epoch], initial ? 0x2003 : 2, initial != 0);
                cupid_video_reset(&gba->video);
                cupid_io_write(gba, REG_DISPCNT, 0x80); /* Keep the display port idle. */
                cupid_io_write(gba, REG_DISPSTAT, 0x10); /* Actual HBlank source. */
                int until = cupid_timing_until(&gba->timing, &gba->video.event);
                CHECK(until > 5);
                cupid_timing_tick(&gba->timing, until + phase - 1);
                uint32_t address = reg ? REG_IME : REG_IE;
                uint16_t value = reg ? (uint16_t)!initial : initial ? 2 : 0x2003;
                int cycles = 0;
                gba->cpu.memory.timed = true;
                /* The phase-zero data strobe shares the display callback's
                 * clock. HBlank announces its later IF acceptance first. */
                if (width == 0) cupid_memory_store8(&gba->cpu,
                    GBA_BASE_IO + address + (reg ? 0 : 1),
                    (int8_t)(reg ? value : value >> 8), &cycles);
                else if (width == 1) cupid_memory_store16(&gba->cpu,
                    GBA_BASE_IO + address, (int16_t)value, &cycles);
                else cupid_memory_store32(&gba->cpu, GBA_BASE_IO + address,
                    value, &cycles);
                gba->cpu.memory.timed = false;
                cupid_gba_process_events(gba);
                CHECK(cycles == 0 && gba->cpu.cycles == 0);
                cupid_timing_tick(&gba->timing, 8);
                uint16_t expectedIE = reg ? (initial ? 0x2003 : 2)
                    : width == 0 ? (initial ? 3 : 0x2002) : value;
                CHECK(cupid_io_read(gba, REG_IE) == expectedIE);
                CHECK(cupid_io_read(gba, REG_IME) == (reg ? !initial : initial));
                CHECK(cupid_io_read(gba, REG_IF) == IRQ_HBLANK);
                ++scenes;
            }
    CHECK(scenes == 540);
    printf("Timed display register scenes: %u\n", scenes);
    cupid_gba_destroy(gba);
    free(gba);
}

int main(int argc, char** argv) {
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "queue") && strcmp(argv[1], "display"))) return 2;
    if (argc == 1 || !strcmp(argv[1], "queue")) RUN(independently_queued_registers);
    if (argc == 1 || !strcmp(argv[1], "display")) RUN(timed_display_register_writes);
    return 0;
}
