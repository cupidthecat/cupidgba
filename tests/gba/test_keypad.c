/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

static const uint64_t epochs[] = {0, UINT32_MAX - 1u, UINT32_MAX, UINT64_C(0x300000000)};
static unsigned observations;

static struct CupidGBA* machine(uint64_t clock) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void write_control(struct CupidGBA* gba, unsigned width, uint16_t value) {
    if (width == 0) cupid_io_write(gba, REG_KEYCNT, value);
    else if (width == 1) {
        /* Disable before changing a two-byte selection. Each byte has its
         * own side effects; this protocol avoids an intermediate request. */
        cupid_io_write8(gba, REG_KEYCNT + 1, (uint8_t)((value >> 8) & ~0x40));
        cupid_io_write8(gba, REG_KEYCNT, (uint8_t)value);
        cupid_io_write8(gba, REG_KEYCNT + 1, (uint8_t)(value >> 8));
    } else {
        /* The low halfword targets the read-only key state. */
        cupid_memory_store32(&gba->cpu, GBA_BASE_IO + REG_KEYINPUT,
                             (int32_t)((uint32_t)value << 16 | 0xFFFF), NULL);
    }
}

static void request(struct CupidGBA* gba, bool expected) {
    CHECK(((gba->irq.pendingIF & IRQ_KEYPAD) != 0) == expected);
    CHECK(!(gba->irqf & IRQ_KEYPAD));
    cupid_timing_tick(&gba->timing, 1);
    CHECK(((gba->irqf & IRQ_KEYPAD) != 0) == expected);
    ++observations;
}

static void acknowledge(struct CupidGBA* gba) {
    cupid_io_write(gba, REG_IF, IRQ_KEYPAD);
    cupid_timing_tick(&gba->timing, 3);
    CHECK(!(gba->irqf & IRQ_KEYPAD));
    CHECK(!(gba->irq.pendingIF & IRQ_KEYPAD));
}

static void levels(unsigned group) {
    struct CupidGBA* gba = machine(0);
    for (unsigned mask = group * 256; mask < (group + 1) * 256; ++mask)
      for (unsigned held = 0; held < 1024; ++held)
        for (unsigned mode = 0; mode < 2; ++mode)
          for (unsigned width = 0; width < 3; ++width) {
            /* Each pair starts with disabled control and unmatched history.
             * Expected levels come from the selected button set, including
             * additional unselected holds and the empty AND selection. */
            gba->memory.io[REG_KEYCNT >> 1] = 0;
            gba->keysLast = 0x400;
            gba->keysActive = (uint16_t)held;
            gba->irqf = gba->irq.pendingIF = 0;
            uint16_t control = (uint16_t)(0x4000 | mode * 0x8000 | mask);
            write_control(gba, width, control | 0x3C00);
            CHECK(cupid_io_read(gba, REG_KEYCNT) == control);
            CHECK(cupid_io_read(gba, REG_KEYINPUT) == (0x3FF ^ held));
            bool selected = false, all = true;
            for (unsigned key = 0; key < 10; ++key) if (mask & (1u << key)) {
                if (held & (1u << key)) selected = true;
                else all = false;
            }
            request(gba, mode ? all : selected);
          }
    CHECK(observations == 1572864);
    destroy(gba);
}

static void transitions(void) {
    for (unsigned epoch = 0; epoch < 4; ++epoch)
      for (unsigned width = 0; width < 3; ++width)
        for (unsigned key = 0; key < 10; ++key) {
            struct CupidGBA* gba = machine(epochs[epoch]);
            uint16_t a = (uint16_t)(1u << key), b = (uint16_t)(1u << ((key + 1) % 10));
            gba->keysActive = a | b;
            write_control(gba, width, 0xC000 | a);
            request(gba, true);
            acknowledge(gba);
            cupid_gba_test_keypad_irq(gba);
            request(gba, false);
            write_control(gba, width, 0xC000 | a);
            request(gba, false);
            /* A new selected held button arms a distinct AND request. */
            write_control(gba, width, 0xC000 | a | b);
            request(gba, true);
            acknowledge(gba);
            write_control(gba, width, 0xC000 | b);
            request(gba, false);
            gba->keysActive = a;
            cupid_gba_test_keypad_irq(gba);
            request(gba, false);
            gba->keysActive = a | b;
            cupid_gba_test_keypad_irq(gba);
            request(gba, true);
            acknowledge(gba);
            /* A disabled poll must retain the unmatched state. */
            write_control(gba, width, a);
            gba->keysActive = 0;
            cupid_gba_test_keypad_irq(gba);
            request(gba, false);
            write_control(gba, width, 0xC000 | b);
            request(gba, false);
            gba->keysActive = b;
            cupid_gba_test_keypad_irq(gba);
            request(gba, true);
            acknowledge(gba);
            write_control(gba, width, 0x4000 | b);
            request(gba, true);
            acknowledge(gba);
            cupid_gba_test_keypad_irq(gba);
            request(gba, true);
            destroy(gba);
        }
    /* Natural high-first byte stores retain an accepted intermediate request
     * even when the final selected button is not held. */
    for (unsigned epoch = 0; epoch < 4; ++epoch) {
        struct CupidGBA* gba = machine(epochs[epoch]);
        gba->keysActive = 1;
        cupid_io_write8(gba, REG_KEYCNT + 1, 0xC0);
        CHECK(gba->irq.pendingIF & IRQ_KEYPAD);
        cupid_io_write8(gba, REG_KEYCNT, 2);
        CHECK(cupid_io_read(gba, REG_KEYCNT) == 0xC002);
        request(gba, true);
        acknowledge(gba);
        cupid_gba_test_keypad_irq(gba);
        request(gba, false);
        destroy(gba);
    }
    CHECK(observations == 1448);
}

static void guest(void) {
    for (unsigned epoch = 0; epoch < 4; ++epoch)
      for (unsigned thumb = 0; thumb < 2; ++thumb)
        for (unsigned width = 0; width < 3; ++width)
          for (unsigned key = 0; key < 10; ++key) {
            struct CupidGBA* gba = machine(epochs[epoch]);
            uint16_t held = (uint16_t)(1u << key), control = 0xC000 | held;
            gba->keysActive = held;
            uint32_t arm[] = {width == 0 ? 0xE1C100B0u : width == 1 ? 0xE5C10000u : 0xE5810000u, 0xE1A00000, 0xEAFFFFFE};
            uint16_t narrow[] = {width == 0 ? 0x8008 : width == 1 ? 0x7008 : 0x6008, 0x46C0, 0xE7FE};
            memcpy(gba->memory.iwram, thumb ? (void*)narrow : (void*)arm, thumb ? sizeof(narrow) : sizeof(arm));
            if (width == 1) {
                /* A high byte is the final strobe after selecting the key. */
                cupid_io_write8(gba, REG_KEYCNT, (uint8_t)control);
                gba->cpu.gprs[0] = control >> 8;
                gba->cpu.gprs[1] = GBA_BASE_IO + REG_KEYCNT + 1;
            } else {
                gba->cpu.gprs[0] = width == 0 ? control : (int32_t)((uint32_t)control << 16 | 0xFFFF);
                gba->cpu.gprs[1] = GBA_BASE_IO + (width == 0 ? REG_KEYCNT : REG_KEYINPUT);
            }
            _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
            gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
            if (thumb) CupidThumbWritePC(&gba->cpu); else CupidARMWritePC(&gba->cpu);
            cupid_gba_step(gba);
            CHECK(cupid_io_read(gba, REG_KEYCNT) == control);
            CHECK(cupid_io_read(gba, REG_KEYINPUT) == (0x3FF ^ held));
            CHECK(gba->irq.pendingIF & IRQ_KEYPAD);
            /* Commit the decoded store's actual outstanding acceptance event. */
            gba->cpu.nextEvent = gba->cpu.cycles;
            gba->cpu.irqh.processEvents(&gba->cpu);
            cupid_timing_tick(&gba->timing, 1);
            CHECK(gba->irqf & IRQ_KEYPAD);
            ++observations;
            destroy(gba);
          }
    CHECK(observations == 240);
}

static void chronology(void) {
    for (unsigned epoch = 0; epoch < 4; ++epoch)
      for (unsigned width = 0; width < 3; ++width)
        for (unsigned enabled = 0; enabled < 2; ++enabled)
          for (unsigned master = 0; master < 2; ++master)
            for (unsigned masked = 0; masked < 2; ++masked) {
                struct CupidGBA* gba = machine(epochs[epoch]);
                gba->cpu.cpsr.i = masked;
                gba->cpu.irqDisableLatch = masked != 0;
                cupid_io_write(gba, REG_IE, enabled ? IRQ_KEYPAD : 0);
                cupid_io_write(gba, REG_IME, master);
                cupid_timing_tick(&gba->timing, 3);
                gba->keysActive = 1;
                write_control(gba, width, 0xC001);
                CHECK(!gba->irq.available && !gba->irq.line);
                request(gba, true);
                CHECK(!gba->irq.available && !gba->irq.line);
                cupid_timing_tick(&gba->timing, 1);
                CHECK(gba->irq.available == (enabled != 0));
                CHECK(!gba->irq.line);
                cupid_timing_tick(&gba->timing, 1);
                CHECK(gba->irq.line == (enabled && master));
                /* An accepted keypad request prevents STOP when IE permits
                 * it, independently of IME and the CPU's IRQ mask. */
                cupid_gba_stop(gba);
                CHECK(gba->stopped == !enabled);
                CHECK(gba->irqf & IRQ_KEYPAD);
                destroy(gba);
            }
    CHECK(observations == 96);
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (argv[1][0] >= '0' && argv[1][0] <= '3' && !argv[1][1]) levels((unsigned)(argv[1][0] - '0'));
    else if (!strcmp(argv[1], "transitions")) transitions();
    else if (!strcmp(argv[1], "guest")) guest();
    else if (!strcmp(argv[1], "chronology")) chronology();
    else CHECK(false);
    printf("Keypad control observations: %u\n", observations);
    return 0;
}
