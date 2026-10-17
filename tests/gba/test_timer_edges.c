/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../test.h"

static struct CupidGBA* gba;
static uint64_t snapshots;

static void reset(void) {
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = 0;
    gba->timing.masterCycles = 0;
    gba->audio.lastSample = 0;
    gba->audio.sampleOrigin = 0;
    gba->audio.nextSample = 512;
}

static void advance_to(uint64_t target) {
    CHECK(target >= gba->timing.globalCycles);
    while (gba->timing.globalCycles < target) {
        uint64_t remaining = target - gba->timing.globalCycles;
        /* Each quiet jump stays inside the scheduler's signed horizon. */
        uint32_t step = remaining > 0x10000000 ? 0x10000000 : (uint32_t)remaining;
        cupid_timing_tick(&gba->timing, step);
    }
    CHECK(gba->timing.masterCycles == (uint32_t)target);
}

static void word(unsigned channel, uint32_t value) {
    cupid_memory_store32(&gba->cpu, GBA_BASE_IO + REG_TMCNT_LO(channel), value, NULL);
}

static void enable(unsigned channel, unsigned width, unsigned selector, uint16_t reload) {
    uint32_t address = GBA_BASE_IO + REG_TMCNT_LO(channel);
    uint16_t control = (uint16_t)(0xC0 | selector);
    if (width == 0) {
        cupid_memory_store8(&gba->cpu, address, (uint8_t)reload, NULL);
        cupid_memory_store8(&gba->cpu, address + 1, (uint8_t)(reload >> 8), NULL);
        cupid_memory_store8(&gba->cpu, address + 2, (uint8_t)control, NULL);
    } else if (width == 1) {
        cupid_memory_store16(&gba->cpu, address, reload, NULL);
        cupid_memory_store16(&gba->cpu, address + 2, control, NULL);
    } else {
        word(channel, reload | ((uint32_t)control << 16));
    }
}

static uint64_t prepare(unsigned channel, unsigned period, unsigned phase, unsigned epoch) {
    const uint64_t boundaries[] = {0, UINT64_C(1) << 31, UINT64_C(1) << 32, UINT64_C(3) << 32};
    reset();
    /* Reach a stopped FFFF through register writes, including its coincident
     * final overflow and stop. No timer counter is assigned by the fixture. */
    word(channel, 0x0080FFFF);
    advance_to(2);
    cupid_io_write(gba, REG_TMCNT_LO(channel) + 2, 0);
    advance_to(3);
    CHECK(cupid_io_read(gba, REG_TMCNT_LO(channel)) == 0xFFFF);
    CHECK(cupid_io_read(gba, REG_TMCNT_LO(channel) + 2) == 0);
    if (channel < 3) word(channel + 1, 0x0084FFFE);
    cupid_io_write(gba, REG_IE, (uint16_t)(IRQ_TIMER0 << channel));
    cupid_io_write(gba, REG_IME, 1);
    uint64_t accepted = (epoch ? boundaries[epoch] - period : 4 * period) + phase;
    advance_to(accepted - 1);
    return accepted;
}

static void check(unsigned channel, unsigned selector, uint16_t counter,
                  uint16_t cascade, bool accepted, bool flags, bool available, bool line) {
    CHECK(cupid_io_read(gba, REG_TMCNT_LO(channel)) == counter);
    CHECK(cupid_io_read(gba, REG_TMCNT_LO(channel) + 2) == (accepted ? (0xC0 | selector) : 0));
    CHECK((channel < 3 ? cupid_io_read(gba, REG_TMCNT_LO(channel + 1)) : 0) == cascade);
    CHECK(cupid_io_read(gba, REG_IF) == (flags ? (IRQ_TIMER0 << channel) : 0));
    CHECK(gba->irq.available == available);
    CHECK(gba->irq.line == line);
    ++snapshots;
}

static void crossed_boundary(unsigned epoch, uint32_t before) {
    uint32_t after = gba->timing.masterCycles;
    if (epoch == 1) CHECK((before ^ after) & 0x80000000);
    if (epoch >= 2) CHECK(after < before);
}

static void enable_scene(unsigned channel, unsigned selector, unsigned period,
                         unsigned width, unsigned epoch, unsigned phase) {
    uint64_t accepted = prepare(channel, period, phase, epoch);
    uint32_t before = gba->timing.masterCycles;
    enable(channel, width, selector, 0xFFFE);
    /* The cascade enable written at clock three is still pending if this
     * first sample also precedes clock four. */
    uint16_t initialCascade = channel < 3 && accepted > 4 ? 0xFFFE : 0;
    check(channel, selector, 0xFFFF, initialCascade, false, false, false, false);
    /* The first overflow gives a distinct deadline from later two-tick
     * periods. A boundary-aligned FFFF takes the pre-reload path. */
    uint64_t firstOverflow = accepted + (phase ? 2 * period - phase + 1 : period);
    for (unsigned elapsed = 0; elapsed <= 2 * period + 4; ++elapsed) {
        if (elapsed > 3 && elapsed != period - 1 && elapsed != period &&
            elapsed != period + 1 && elapsed != period + 2 && elapsed != period + 3 &&
            elapsed != 2 * period - 1 && elapsed != 2 * period &&
            elapsed != 2 * period + 1 && elapsed != 2 * period + 2 &&
            elapsed != 2 * period + 3 && elapsed != 2 * period + 4) continue;
        uint64_t now = accepted + elapsed;
        advance_to(now);
        uint64_t overflows = now < firstOverflow ? 0 : 1 + (now - firstOverflow) / (2 * period);
        uint16_t counter;
        if (overflows) {
            counter = (uint16_t)(0xFFFE + ((now - firstOverflow) % (2 * period)) / period);
        } else {
            uint64_t firstIncrement = phase ? firstOverflow - period : firstOverflow;
            unsigned increments = now < firstIncrement ? 0 : 1 + (unsigned)((now - firstIncrement) / period);
            counter = (uint16_t)((phase ? 0xFFFE : 0xFFFF) + increments);
        }
        uint16_t cascade = channel < 3 ? (uint16_t)(0xFFFE + overflows % 2) : 0;
        check(channel, selector, counter, cascade, true, now >= firstOverflow + 1,
              now >= firstOverflow + 2, now >= firstOverflow + 3);
    }
    crossed_boundary(epoch, before);
}

static void acknowledge(unsigned channel, unsigned width) {
    uint16_t bit = (uint16_t)(IRQ_TIMER0 << channel);
    if (width == 0) cupid_memory_store8(&gba->cpu, GBA_BASE_IO + REG_IF, (uint8_t)bit, NULL);
    else if (width == 1) cupid_memory_store16(&gba->cpu, GBA_BASE_IO + REG_IF, bit, NULL);
    else cupid_memory_store32(&gba->cpu, GBA_BASE_IO + REG_IE, bit | ((uint32_t)bit << 16), NULL);
}

static void collision_scene(unsigned channel, unsigned selector, unsigned period,
                            unsigned width, unsigned epoch, unsigned order) {
    uint64_t accepted = prepare(channel, period, 0, epoch);
    uint32_t before = gba->timing.masterCycles;
    /* Keep later overflows outside this six-clock collision observation. */
    enable(channel, width, selector, 0xFFF8);
    uint64_t overflow = accepted + period;
    for (int offset = -1; offset <= 4; ++offset) {
        uint64_t now = offset < 0 ? overflow - 1 : overflow + (unsigned)offset;
        advance_to(now);
        if (offset == (int)order - 1) acknowledge(channel, width);
        bool flags = order == 0 ? offset >= 1 : order == 1 ? false : offset == 1;
        bool available = order == 0 ? offset >= 2 : order == 1 ? false : offset == 2;
        bool line = order == 0 ? offset >= 3 : order == 1 ? false : offset == 3;
        uint16_t counter = offset < 0 ? 0xFFFF : (uint16_t)(0xFFF8 + (unsigned)offset / period);
        uint16_t cascade = channel < 3 ? (offset < 0 ? 0xFFFE : 0xFFFF) : 0;
        check(channel, selector, counter, cascade, true, flags, available, line);
    }
    crossed_boundary(epoch, before);
}

static unsigned argument(const char* text) {
    char* end;
    unsigned long value = strtoul(text, &end, 10);
    CHECK(end != text && *end == 0 && value < 4);
    return (unsigned)value;
}

int main(int argc, char** argv) {
    CHECK(argc == 3);
    unsigned channel = argument(argv[1]), selector = argument(argv[2]);
    const unsigned periods[] = {1, 64, 256, 1024};
    unsigned period = periods[selector];
    gba = calloc(1, sizeof(*gba)); CHECK(gba);
    cupid_gba_create(gba);
    for (unsigned width = 0; width < 3; ++width) for (unsigned epoch = 0; epoch < 4; ++epoch) {
        for (unsigned phase = 0; phase < period; ++phase) {
            enable_scene(channel, selector, period, width, epoch, phase);
        }
        for (unsigned order = 0; order < 3; ++order) {
            collision_scene(channel, selector, period, width, epoch, order);
        }
    }
    cupid_gba_destroy(gba); free(gba);
    printf("channel %u /%u: %u enable scenes, 36 collision scenes; %" PRIu64 " six-field snapshots\n",
           channel, period, 12 * period, snapshots);
    return 0;
}
