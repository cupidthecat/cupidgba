/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../../test.h"

static unsigned component_frame(struct CupidGyro* gyro) {
    unsigned result = 0;
    for (unsigned bit = 0; bit < 16; bit++) {
        const uint8_t pins = cupid_gyro_output_pins(gyro);
        CHECK(!(pins & ~4));
        result = result * 2 + (pins >> 2);
        cupid_gyro_write_pins(gyro, 2);
        CHECK(cupid_gyro_output_pins(gyro) == pins);
        cupid_gyro_write_pins(gyro, 0);
        const uint8_t next = cupid_gyro_output_pins(gyro);
        cupid_gyro_write_pins(gyro, 0);
        CHECK(cupid_gyro_output_pins(gyro) == next);
        cupid_gyro_write_pins(gyro, 2);
    }
    return result;
}

static void every_converter_value_has_a_complete_msb_first_frame(void) {
    struct CupidGyro gyro;
    cupid_gyro_init(&gyro);
    CHECK(gyro.sample == CUPID_GYRO_NEUTRAL);
    for (unsigned value = 0; value < 4096; value++) {
        CHECK(cupid_gyro_set_sample(&gyro, (uint16_t)value));
        cupid_gyro_write_pins(&gyro, 3);
        cupid_gyro_write_pins(&gyro, 2);
        CHECK(component_frame(&gyro) == value);
        CHECK(component_frame(&gyro) == 0); /* Overrun does not repeat the frame. */
    }
    CHECK(!cupid_gyro_set_sample(&gyro, 4096) && gyro.sample == 4095);
    CHECK(!cupid_gyro_set_sample(&gyro, UINT16_MAX) && gyro.sample == 4095);
}

static void input_changes_restart_and_reset_keep_snapshot_ownership(void) {
    struct CupidGyro gyro;
    cupid_gyro_init(&gyro);
    CHECK(cupid_gyro_set_sample(&gyro, 0xA55));
    cupid_gyro_write_pins(&gyro, 3);
    cupid_gyro_write_pins(&gyro, 2);
    CHECK(cupid_gyro_set_sample(&gyro, 0x5AA));
    CHECK(component_frame(&gyro) == 0xA55);
    cupid_gyro_write_pins(&gyro, 3);
    cupid_gyro_write_pins(&gyro, 2);
    CHECK(component_frame(&gyro) == 0x5AA);
    for (unsigned prefix = 0; prefix < 16; prefix++) {
        cupid_gyro_write_pins(&gyro, 3);
        cupid_gyro_write_pins(&gyro, 2);
        for (unsigned bit = 0; bit < prefix; bit++) {
            cupid_gyro_write_pins(&gyro, 0);
            cupid_gyro_write_pins(&gyro, 2);
        }
        cupid_gyro_write_pins(&gyro, 3);
        cupid_gyro_write_pins(&gyro, 2);
        CHECK(component_frame(&gyro) == 0x5AA);
    }
    cupid_gyro_reset_bus(&gyro);
    CHECK(gyro.sample == 0x5AA && component_frame(&gyro) == 0);
}

static struct CupidGBA* machine(const char* board) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    gba->memory.rom = calloc(1, 4096);
    CHECK(gba->memory.rom);
    gba->memory.romSize = gba->memory.romImageSize = 4096;
    gba->memory.romMask = 4095;
    memcpy(gba->memory.rom + 0xAC, board, 4);
    gba->memory.rom[0xC4] = 0xBA;
    gba->memory.rom[0xC5] = 0xDC;
    cupid_cartridge_attach(gba, gba->memory.rom, 4096);
    cupid_gba_reset(gba);
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static unsigned port_frame(struct CupidGBA* gba, uint32_t address, bool word, unsigned motor) {
    unsigned result = 0;
    for (unsigned bit = 0; bit < 16; bit++) {
        const uint32_t pins = word ? (uint32_t)cupid_memory_load32(&gba->cpu, address, NULL)
                                   : cupid_memory_loadU16(&gba->cpu, address, NULL);
        CHECK((pins & 11) == (motor | 2));
        if (word) CHECK((pins >> 16) == 11);
        result = result * 2 + ((pins >> 2) & 1);
        if (word) {
            cupid_memory_store32(&gba->cpu, address, 0x000B0000 | motor, NULL);
            cupid_memory_store32(&gba->cpu, address, 0x000B0002 | motor, NULL);
        } else {
            cupid_memory_store16(&gba->cpu, address, (int16_t)motor, NULL);
            cupid_memory_store16(&gba->cpu, address, (int16_t)(motor | 2), NULL);
        }
        CHECK(gba->cartridge.rumbleActive == (motor != 0));
    }
    return result;
}

static void board_detection_bus_widths_and_motor_share_the_port(void) {
    const char* boards[] = {"RZWJ", "RZWE", "RZWP"};
    for (unsigned board = 0; board < 3; board++)
        for (unsigned bank = 0; bank < 3; bank++) for (unsigned word = 0; word < 2; word++) {
            struct CupidGBA* gba = machine(boards[board]);
            CHECK(gba->cartridge.gyroPresent && gba->cartridge.rumblePresent);
            CHECK(!gba->cartridge.rtcPresent && !gba->cartridge.solarPresent);
            const uint32_t address = GBA_BASE_ROM0 + bank * 0x02000000 + 0xC4;
            CHECK(cupid_memory_loadU16(&gba->cpu, address, NULL) == 0xDCBA);
            cupid_memory_store16(&gba->cpu, address + 4, 1, NULL);
            cupid_memory_store16(&gba->cpu, address + 2, 11, NULL);
            for (unsigned motor = 0; motor <= 8; motor += 8) {
                CHECK(cupid_cartridge_set_gyro_sample(gba, 0xA53));
                cupid_memory_store16(&gba->cpu, address, (int16_t)(3 | motor), NULL);
                cupid_memory_store16(&gba->cpu, address, (int16_t)(2 | motor), NULL);
                CHECK(port_frame(gba, address, word != 0, motor) == 0xA53);
                CHECK(port_frame(gba, address, word != 0, motor) == 0);
            }
            cupid_memory_store16(&gba->cpu, address + 4, 0, NULL);
            CHECK(cupid_memory_loadU16(&gba->cpu, address, NULL) == 0xDCBA);
            CHECK(gba->memory.rom[0xC4] == 0xBA && gba->memory.rom[0xC5] == 0xDC);
            destroy(gba);
        }
}

static void reset_disable_and_replacement_preserve_only_external_input(void) {
    struct CupidGBA* gba = machine("RZWE");
    CHECK(cupid_cartridge_set_gyro_sample(gba, 0x800));
    cupid_memory_store16(&gba->cpu, 0x080000C8, 1, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C6, 11, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C4, 3, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C4, 2, NULL);
    for (unsigned bit = 0; bit < 4; bit++) {
        cupid_cartridge_gpio_write(gba, 0x080000C4, 0);
        cupid_cartridge_gpio_write(gba, 0x080000C4, 2);
    }
    CHECK(cupid_memory_loadU16(&gba->cpu, 0x080000C4, NULL) == 6);
    cupid_gba_reset(gba);
    CHECK(gba->cartridge.gyroPresent && gba->cartridge.gyro.sample == 0x800);
    CHECK(!gba->cartridge.gyro.output && !gba->cartridge.gyro.shift);
    cupid_cartridge_enable_gyro(gba, false);
    CHECK(!gba->cartridge.gyroPresent && gba->cartridge.gyro.sample == 0x800);
    CHECK(gba->cartridge.rumblePresent);
    memcpy(gba->memory.rom + 0xAC, "V49E", 4);
    cupid_cartridge_attach(gba, gba->memory.rom, 4096);
    CHECK(!gba->cartridge.gyroPresent && gba->cartridge.rumblePresent);
    CHECK(gba->cartridge.gyro.sample == 0x800);
    memcpy(gba->memory.rom + 0xAC, "RZWX", 4);
    cupid_cartridge_attach(gba, gba->memory.rom, 4096);
    CHECK(!gba->cartridge.gyroPresent && !gba->cartridge.rumblePresent);
    cupid_cartridge_enable_gyro(gba, true);
    CHECK(gba->cartridge.gyroPresent && gba->cartridge.gyro.sample == 0x800);
    destroy(gba);
}

struct ClockEdge { struct CupidGBA* gba; unsigned calls; uint64_t clock; };
static void serial_edge(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct ClockEdge* edge = context;
    edge->clock = cupid_timing_global_time(timing);
    edge->calls++;
    cupid_cartridge_gpio_write(edge->gba, 0x080000C4, 0);
    cupid_cartridge_gpio_write(edge->gba, 0x080000C4, 2);
}

static void dma_reads_at_its_strobe_before_a_later_serial_edge(void) {
    for (unsigned wrap = 0; wrap < 2; wrap++) {
        struct CupidGBA* gba = machine("RZWE");
        CHECK(cupid_cartridge_set_gyro_sample(gba, 0x800));
        cupid_cartridge_gpio_write(gba, 0x080000C8, 1);
        cupid_cartridge_gpio_write(gba, 0x080000C6, 11);
        cupid_cartridge_gpio_write(gba, 0x080000C4, 3);
        cupid_cartridge_gpio_write(gba, 0x080000C4, 2);
        for (unsigned bit = 0; bit < 4; bit++) {
            cupid_cartridge_gpio_write(gba, 0x080000C4, 0);
            cupid_cartridge_gpio_write(gba, 0x080000C4, 2);
        }
        cupid_timing_clear(&gba->timing);
        const uint64_t base = wrap ? UINT32_MAX - 6u : 0;
        gba->timing.globalCycles = base;
        gba->timing.masterCycles = (uint32_t)base;
        struct ClockEdge edge = {.gba = gba};
        struct CupidTimingEvent event = {.context = &edge, .callback = serial_edge, .priority = 0x60};
        cupid_timing_schedule(&gba->timing, &event, 9);
        cupid_dma_write_sad(gba, 3, 0x080000C4);
        cupid_dma_write_dad(gba, 3, GBA_BASE_IWRAM);
        cupid_dma_write_cnt_lo(gba, 3, 1);
        cupid_dma_write_cnt(gba, 3, 0xC400);
        cupid_timing_tick(&gba->timing, 32);
        CHECK(edge.calls == 1 && edge.clock == base + 9);
        CHECK((uint32_t)cupid_memory_load32(&gba->cpu, GBA_BASE_IWRAM, NULL) == 0x000B0006);
        CHECK(cupid_memory_loadU16(&gba->cpu, 0x080000C4, NULL) == 2);
        CHECK(gba->irqf & IRQ_DMA3);
        destroy(gba);
    }
}

int main(void) {
    RUN(every_converter_value_has_a_complete_msb_first_frame);
    RUN(input_changes_restart_and_reset_keep_snapshot_ownership);
    RUN(board_detection_bus_widths_and_motor_share_the_port);
    RUN(reset_disable_and_replacement_preserve_only_external_input);
    RUN(dma_reads_at_its_strobe_before_a_later_serial_edge);
    return 0;
}
