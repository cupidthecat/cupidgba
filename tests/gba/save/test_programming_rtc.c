/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../../test.h"
#include <string.h>

static const uint64_t anchors[] = {0, INT32_MAX - 19u, UINT32_MAX - 19u, UINT64_C(0x300000000)};
static const uint8_t payload[8] = {0xA5, 0x31, 0x02, 0xC7, 0x11, 0x80, 0x5A, 0xE9};

static void flash_command(struct CupidSavedata* save, uint8_t command) {
    cupid_savedata_write_flash(save, 0x5555, 0xAA);
    cupid_savedata_write_flash(save, 0x2AAA, 0x55);
    cupid_savedata_write_flash(save, 0x5555, command);
}

static void accept_operation(struct CupidSavedata* save, unsigned operation) {
    if (!operation) {
        uint16_t wire[73]; unsigned n = 0;
        wire[n++] = 1; wire[n++] = 0;
        for (unsigned i = 0; i < 6; i++) wire[n++] = 1;
        for (unsigned i = 0; i < 64; i++) wire[n++] = (payload[i / 8] >> (7 - i % 8)) & 1;
        wire[n++] = 0;
        CHECK(n == 73);
        for (unsigned i = 0; i < n; i++) cupid_savedata_write_eeprom(save, wire[i], n - i);
    } else {
        flash_command(save, 0xB0);
        cupid_savedata_write_flash(save, 0, 1);
        if (operation == 1) {
            flash_command(save, 0xA0);
            cupid_savedata_write_flash(save, 0x2345, payload[0]);
        } else {
            flash_command(save, 0x80);
            if (operation == 2) {
                /* Sector selection belongs to the sixth command strobe. */
                cupid_savedata_write_flash(save, 0x5555, 0xAA);
                cupid_savedata_write_flash(save, 0x2AAA, 0x55);
                cupid_savedata_write_flash(save, 0x2345, 0x30);
            } else flash_command(save, 0x10);
        }
    }
    CHECK(save->programming && !save->dirty);
}

static void storage(const struct CupidSavedata* save, unsigned operation, bool complete) {
    const uint32_t start = operation == 0 ? 0x1F8 : operation == 1 ? 0x12345 : operation == 2 ? 0x12000 : 0;
    const uint32_t length = operation == 0 ? 8 : operation == 1 ? 1 : operation == 2 ? 0x1000 : 0x20000;
    CHECK(save->programOffset == start && save->programLength == length);
    for (size_t i = 0; i < save->size; i++) {
        uint8_t wanted = operation < 2 ? 0xFF : 0;
        if (complete && i >= start && i - start < length)
            wanted = operation == 0 ? payload[i - start] : operation == 1 ? payload[0] : 0xFF;
        CHECK(save->data[i] == wanted);
    }
    CHECK(save->dirty == complete && save->programming != complete);
}

int main(void) {
    const unsigned wake[] = {4, 11, 17};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
    for (unsigned operation = 0; operation < 4; operation++)
    for (unsigned w = 0; w < 3; w++)
    for (unsigned partition = 0; partition < 2; partition++)
    for (unsigned ime = 0; ime < 2; ime++) {
        struct CupidGBA* gba = calloc(1, sizeof(*gba));
        CHECK(gba);
        cupid_gba_create(gba);
        cupid_gba_reset(gba);
        cupid_timing_clear(&gba->timing);
        gba->timing.globalCycles = anchors[epoch];
        gba->timing.masterCycles = (uint32_t)anchors[epoch];
        cupid_rtc_rebase(&gba->cartridge.rtc, anchors[epoch]);
        cupid_cartridge_enable_rtc(gba, true);
        cupid_savedata_force_type(&gba->memory.savedata, operation ? SAVE_FLASH1M : SAVE_EEPROM512);
        struct CupidSavedata* save = &gba->memory.savedata;
        CHECK(cupid_savedata_set_programming_clocks(save, 11, 11, 11, 11));
        memset(save->data, operation < 2 ? 0xFF : 0, save->size);
        cupid_io_write(gba, REG_TM0CNT_L, 0x8000);
        cupid_io_write(gba, REG_TM0CNT_H, 0x80);
        cupid_irq_write(gba, REG_IE, IRQ_GAMEPAK);
        cupid_irq_write(gba, REG_IME, (uint16_t)ime);
        gba->cpu.cycles += 8;
        gba->cpu.irqh.processEvents(&gba->cpu);
        accept_operation(save, operation);
        const uint64_t clock = cupid_timing_global_time(&gba->timing);
        CHECK(save->readyClock == clock + 11);
        cupid_gba_stop(gba);
        CHECK(gba->stopped && gba->cpu.halted && !gba->irq.pendingIF);
        cupid_rtc_write_interrupt_parameters(&gba->cartridge.rtc, 1);
        gba->cartridge.rtc.fraction = 256 - wake[w];
        cupid_rtc_write_control(&gba->cartridge.rtc, 0x42);
        cupid_cartridge_rtc_reschedule(gba);
        const uint16_t timer = cupid_timer_read_counter(gba, 0);
        const uint32_t timer_deadline = gba->timers[0].event.when;
        const uint16_t line = gba->video.vcount;
        const int samples = gba->audio.sampleWritePos;
        uint64_t elapsed = 0;
        if (partition) {
            const unsigned first = wake[w] > 10 ? 10 : wake[w] - 1;
            CHECK(cupid_gba_advance_stopped_time(gba, first) == first);
            elapsed += first;
            CHECK(gba->stopped);
            storage(save, operation, false);
            if (wake[w] > 11) {
                CHECK(cupid_gba_advance_stopped_time(gba, 1) == 1);
                elapsed++;
                CHECK(gba->stopped);
                storage(save, operation, true);
            }
        }
        elapsed += cupid_gba_advance_stopped_time(gba, 32);
        CHECK(elapsed == wake[w] && !gba->stopped && gba->cpu.halted);
        CHECK(cupid_gba_advance_stopped_time(gba, 32) == 0);
        CHECK(cupid_timing_global_time(&gba->timing) == clock);
        CHECK(cupid_timer_read_counter(gba, 0) == timer && gba->timers[0].event.when == timer_deadline);
        CHECK(gba->video.vcount == line && gba->audio.sampleWritePos == samples);
        CHECK(gba->irq.pendingIF == IRQ_GAMEPAK && !gba->irqf && !gba->irq.available);
        CHECK(gba->cartridge.rtc.fraction == 256);
        storage(save, operation, wake[w] >= 11);
        if (wake[w] < 11) {
            CHECK(save->readyClock == clock + 11 - wake[w]);
            cupid_timing_tick(&gba->timing, 10 - (int32_t)wake[w]);
            storage(save, operation, false);
            cupid_timing_tick(&gba->timing, 1);
            storage(save, operation, true);
        }
        cupid_gba_destroy(gba);
        free(gba);
        scenes++;
    }
    CHECK(scenes == 192);
    puts("PASS 192 complete save/RTC STOP scenes: partial wake, ready boundaries, IME, dispatch partitions and clock wrap");
    return 0;
}
