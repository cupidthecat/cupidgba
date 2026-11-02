/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_card_reader.c
 * @brief Card-reader bus widths, serial transactions, scan IRQs and input ownership.
 */
#include "../../test.h"
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"

static const uint32_t control0 = 0x0E00FFB0, control1 = 0x0E00FFB1;
static const uint64_t epochs[] = {0, INT32_MAX - 1000u, UINT32_MAX - 1000u, UINT64_C(0x300000000)};

static struct CupidGBA *machine(unsigned epoch) {
    struct CupidGBA *gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = epochs[epoch];
    gba->timing.masterCycles = (uint32_t)epochs[epoch];
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    gba->memory.rom = calloc(1, 4096);
    CHECK(gba->memory.rom);
    gba->memory.romSize = 4096;
    gba->memory.romImageSize = 4096;
    gba->memory.romMask = 4095;
    cupid_cartridge_enable_card_reader(gba, true);
    return gba;
}

static void destroy(struct CupidGBA *gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void pins(struct CupidGBA *gba, unsigned value) {
    cupid_memory_store8(&gba->cpu, control0, (uint8_t)value, NULL);
}

static void serial_pins(struct CupidGBA *gba, unsigned value) {
    pins(gba, value | 0x60);
}

static void begin(struct CupidGBA *gba) {
    cupid_io_write(gba, REG_WAITCNT, 0x1800);
    serial_pins(gba, 7);
    serial_pins(gba, 6);
    serial_pins(gba, 4);
}

static void end(struct CupidGBA *gba) {
    serial_pins(gba, 4);
    serial_pins(gba, 6);
    serial_pins(gba, 7);
}

static void send(struct CupidGBA *gba, unsigned value) {
    for (int bit = 7; bit >= 0; bit--) {
        const unsigned data = 4 | ((value >> bit) & 1);
        serial_pins(gba, data);
        serial_pins(gba, data | 2);
        serial_pins(gba, data);
    }
    serial_pins(gba, 0);
    serial_pins(gba, 2);
    CHECK(!(cupid_memory_load8(&gba->cpu, control0, NULL) & 1));
    serial_pins(gba, 0);
    serial_pins(gba, 4);
}

static unsigned receive(struct CupidGBA *gba, bool last) {
    unsigned value = 0;
    for (unsigned bit = 0; bit < 8; bit++) {
        serial_pins(gba, 0);
        serial_pins(gba, 2);
        value = (value << 1) | (cupid_memory_load8(&gba->cpu, control0, NULL) & 1);
        serial_pins(gba, 0);
    }
    serial_pins(gba, 4 | last);
    serial_pins(gba, 6 | last);
    serial_pins(gba, 4 | last);
    return value;
}

static void write_camera(struct CupidGBA *gba, unsigned index, unsigned value) {
    begin(gba);
    send(gba, 0x22);
    send(gba, index);
    send(gba, value);
    end(gba);
}

static void read_camera(struct CupidGBA *gba, unsigned index, uint8_t *data, unsigned count) {
    begin(gba);
    send(gba, 0x22);
    send(gba, index);
    begin(gba);
    send(gba, 0x23);
    for (unsigned i = 0; i < count; i++)
        data[i] = (uint8_t)receive(gba, i + 1 == count);
    end(gba);
}

static void serial(void) {
    struct CupidGBA *gba = machine(0);
    uint8_t values[128];
    read_camera(gba, 0, values, 1);
    CHECK(values[0] == 0x12);
    write_camera(gba, 0, 0xFF);
    read_camera(gba, 0x80, values, 1);
    CHECK(values[0] == 0x12);
    for (unsigned value = 0; value < 256; value++) {
        write_camera(gba, 0x30, value);
        read_camera(gba, 0xB0, values, 1);
        CHECK(values[0] == value);
        write_camera(gba, 0xD3, value);
        read_camera(gba, 0x53, values, 1);
        CHECK(values[0] == (value & 3));
    }
    begin(gba);
    send(gba, 0x22);
    send(gba, 0x50);
    for (unsigned i = 0; i < 11; i++)
        send(gba, 0xA0 + i);
    end(gba);
    read_camera(gba, 0x50, values, 11);
    const uint8_t expected[] = {0xA0, 0xA1, 0xA2, 3, 0, 1, 0xA6, 0, 0, 0, 0};
    CHECK(!memcmp(values, expected, sizeof(expected)));
    write_camera(gba, 0x70, 0xFF);
    read_camera(gba, 0x70, values, 1);
    CHECK(values[0] == 0);
    /* A truncated byte and repeated START must not leak its partial bits. */
    begin(gba);
    serial_pins(gba, 5);
    serial_pins(gba, 7);
    serial_pins(gba, 5);
    begin(gba);
    send(gba, 0x22);
    send(gba, 0x10);
    send(gba, 0x5A);
    end(gba);
    read_camera(gba, 0x10, values, 1);
    CHECK(values[0] == 0x5A);
    /* Long reads cross holes, the 7-bit mirror and the 8-bit index wrap. */
    read_camera(gba, 0xF8, values, sizeof(values));
    CHECK(values[8] == 0x12);
    CHECK(gba->cartridge.cardReader.index == 0x78);
    destroy(gba);
}

static uint32_t access(struct CupidGBA *gba, bool thumb, unsigned width, uint32_t address,
                       uint32_t value, bool load) {
    const uint16_t thumbLoad[] = {0x7801, 0x8801, 0x6801};
    const uint16_t thumbStore[] = {0x7001, 0x8001, 0x6001};
    const uint32_t armLoad[] = {0xE5D01000, 0xE1D010B0, 0xE5901000};
    const uint32_t armStore[] = {0xE5C01000, 0xE1C010B0, 0xE5801000};
    const unsigned slot = width == 4 ? 2 : width - 1;
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    if (thumb) {
        const uint16_t code[] = {load ? thumbLoad[slot] : thumbStore[slot], 0x46C0, 0x46C0};
        memcpy(gba->memory.iwram + 0x1000, code, sizeof(code));
        gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x1000;
        CupidThumbWritePC(&gba->cpu);
    } else {
        const uint32_t code[] = {load ? armLoad[slot] : armStore[slot], 0xE1A00000, 0xE1A00000};
        memcpy(gba->memory.iwram + 0x1000, code, sizeof(code));
        gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x1000;
        CupidARMWritePC(&gba->cpu);
    }
    gba->cpu.gprs[0] = (int32_t)address;
    gba->cpu.gprs[1] = (int32_t)value;
    cupid_gba_step(gba);
    const int32_t cycles = gba->cpu.cycles;
    gba->cpu.cycles = 0;
    if (cycles)
        cupid_timing_tick(&gba->timing, cycles);
    return (uint32_t)gba->cpu.gprs[1];
}

static void ports(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned thumb = 0; thumb < 2; thumb++)
            for (unsigned width = 1; width <= 4; width *= 2) {
                struct CupidGBA *gba = machine(epoch);
                access(gba, thumb != 0, width, 0x0DF80026, width == 4 ? 0x000B0005 : 0xB5, false);
                CHECK(access(gba, thumb != 0, width, 0x0DF9FFFC, 0, true) == (width == 4
                                                                                  ? 0x000B000B
                                                                              : width == 2 ? 5
                                                                                           : 5));
                for (unsigned i = 0; i < CUPID_CARD_SENSOR_WORDS; i++)
                    gba->cartridge.cardReader.sensor[i] = (uint16_t)(0x1100 + i);
                CHECK(access(gba, thumb != 0, 4, 0x0DFC0100, 0, true) == 0x11011100);
                CHECK(access(gba, thumb != 0, 2, 0x0DFDFF88, 0, true) == 0x44);
                CHECK(access(gba, thumb != 0, 2, 0x0DFC008A, 0, true) == 0);
                access(gba, thumb != 0, width, 0x0DFC0000, 0xFFFF, false);
                CHECK(gba->cartridge.cardReader.sensor[0] == 0x1100);
                access(gba, thumb != 0, width, 0x0E00FF83, 0xFFFFFFFF, false);
                CHECK(access(gba, thumb != 0, width, 0x0F00FFC3, 0, true) ==
                      (width == 4   ? 0x7F7F7F7F
                       : width == 2 ? 0x7F00007F
                                    : 0x7F));
                CHECK(gba->memory.savedata.type == SAVE_AUTODETECT);
                CHECK(gba->memory.rom[0] == 0);
                destroy(gba);
                scenes++;
            }
    CHECK(scenes == 24);
    printf("card-reader instruction port scenes=%u\n", scenes);
}

static void start_scan(struct CupidGBA *gba) {
    uint8_t image[3520];
    memset(image, 0xFF, sizeof(image));
    CHECK(cupid_cartridge_queue_card(gba, image, sizeof(image)));
    write_camera(gba, 0x14, 0);
    write_camera(gba, 0x15, 16);
    cupid_memory_store8(&gba->cpu, control0 + 2, 8, NULL);
    cupid_memory_store8(&gba->cpu, control0 + 3, 0, NULL);
    pins(gba, 0x78);
    CHECK(gba->cartridge.cardReader.queueCount == 0);
    CHECK(gba->cartridge.cardReader.scanX == -24);
    for (unsigned line = 0; line < 42; line++)
        cupid_memory_store8(&gba->cpu, control1, 0, NULL);
    CHECK(gba->cartridge.cardReader.scanX == 396 && gba->cartridge.cardReader.scanY == 10);
    for (unsigned word = 0; word < 20; word++)
        CHECK(gba->cartridge.cardReader.sensor[word] == (word < 10    ? 0xFFFF
                                                         : word == 10 ? 0x00F0
                                                                      : 0));
}

static void scan(void) {
    for (unsigned epoch = 0; epoch < 4; epoch++) {
        struct CupidGBA *gba = machine(epoch);
        start_scan(gba);
        CHECK(cupid_memory_load8(&gba->cpu, control1, NULL) == 0x82);
        CHECK(!(gba->irq.pendingIF & IRQ_GAMEPAK));
        cupid_timing_tick(&gba->timing, 15);
        CHECK(!(gba->irq.pendingIF & IRQ_GAMEPAK));
        cupid_timing_tick(&gba->timing, 1);
        CHECK(gba->irq.pendingIF & IRQ_GAMEPAK);
        CHECK(!(gba->irqf & IRQ_GAMEPAK));
        cupid_timing_tick(&gba->timing, 1);
        CHECK(gba->irqf & IRQ_GAMEPAK);
        cupid_irq_write(gba, REG_IF, IRQ_GAMEPAK);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(!(gba->irqf & IRQ_GAMEPAK));
        cupid_memory_store8(&gba->cpu, control1, 0, NULL);
        CHECK(cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.cardReader.irqEvent));
        pins(gba, 4);
        CHECK(!cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.cardReader.irqEvent));
        cupid_timing_tick(&gba->timing, 32);
        CHECK(!(gba->irqf & IRQ_GAMEPAK));
        destroy(gba);
    }
}

static void dma(void) {
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned width = 2; width <= 4; width += 2) {
            struct CupidGBA *gba = machine(epoch);
            start_scan(gba);
            uint16_t expected[20];
            memcpy(expected, gba->cartridge.cardReader.sensor, sizeof(expected));
            cupid_dma_write_sad(gba, 3, 0x0DFC0000);
            cupid_dma_write_dad(gba, 3, GBA_BASE_IWRAM + 0x100);
            cupid_dma_write_cnt_lo(gba, 3, width == 4 ? 10 : 20);
            cupid_dma_write_cnt(gba, 3, width == 4 ? 0x8400 : 0x8000);
            unsigned clocks = 0;
            while (gba->memory.dma[3].enable && clocks < 512) {
                cupid_timing_tick(&gba->timing, 1);
                clocks++;
            }
            CHECK(clocks < 512 && !gba->memory.dma[3].enable);
            for (unsigned half = 0; half < 20; half++)
                CHECK(cupid_memory_loadU16(&gba->cpu, GBA_BASE_IWRAM + 0x100 + half * 2, NULL) ==
                      expected[half]);
            CHECK(gba->cartridge.cardReader.control[1] & 2);
            destroy(gba);
        }
}

static void lifecycle(void) {
    struct CupidGBA *gba = machine(0);
    uint8_t image[1872] = {0};
    CHECK(cupid_cartridge_queue_card(gba, image, sizeof(image)));
    CHECK(gba->cartridge.cardReader.queued[0][200 + 1] == 1);
    memset(image, 0xFF, sizeof(image));
    CHECK(gba->cartridge.cardReader.queued[0][200 + 1] == 1);
    for (unsigned i = 1; i < CUPID_CARD_QUEUE; i++)
        CHECK(cupid_cartridge_queue_card(gba, image, sizeof(image)));
    struct CupidCardReader before = gba->cartridge.cardReader;
    CHECK(!cupid_cartridge_queue_card(gba, image, sizeof(image)));
    CHECK(!memcmp(&before, &gba->cartridge.cardReader, sizeof(before)));
    cupid_card_reader_reset(&gba->cartridge.cardReader);
    CHECK(gba->cartridge.cardReader.queueCount == CUPID_CARD_QUEUE);
    pins(gba, 0x78);
    CHECK(gba->cartridge.cardReader.queueCount == CUPID_CARD_QUEUE - 1);
    cupid_cartridge_enable_card_reader(gba, false);
    CHECK(!gba->cartridge.cardReader.dots && !gba->cartridge.cardReader.queueCount);
    CHECK(!gba->cpu.memory.readOpcode);
    CHECK(!cupid_cartridge_queue_card(gba, image, sizeof(image)));
    cupid_cartridge_enable_card_reader(gba, true);
    CHECK(!cupid_cartridge_queue_card(gba, image, sizeof(image) - 1));
    CHECK(gba->cartridge.cardReader.queueCount == 0);
    destroy(gba);
}

static void calibration(void) {
    struct CupidGBA *gba = machine(0);
    gba->memory.savedata.type = SAVE_FLASH1M;
    cupid_savedata_init_flash(&gba->memory.savedata);
    gba->memory.savedata.data[0xD001] = 0x5A;
    uint8_t original[4096];
    memcpy(original, gba->memory.savedata.data + 0xD000, sizeof(original));
    CHECK(cupid_memory_load8(&gba->cpu, 0x0E00E000, NULL) == 'C');
    CHECK(!memcmp(original, gba->memory.savedata.data + 0xD000, sizeof(original)));
    const uint8_t *sector = gba->memory.savedata.data + 0xE000;
    CHECK(!memcmp(sector, "Card-E Reader 2001", 18));
    CHECK(sector[0x52] == 1 && sector[0x16] == 0x30);
    unsigned sum = 0;
    for (unsigned byte = 0; byte < 4096; byte += 2)
        sum += sector[byte] | (unsigned)sector[byte + 1] << 8;
    CHECK(((sum + (sum >> 16)) & 0xFFFF) == 0xFFFF);
    memset(gba->memory.savedata.data + 0xE000, 0xFF, 4096);
    cupid_card_reader_reset(&gba->cartridge.cardReader);
    CHECK(cupid_memory_load8(&gba->cpu, 0x0E00E000, NULL) == 0xFF);
    destroy(gba);
    gba = machine(0);
    gba->memory.savedata.type = SAVE_FLASH1M;
    cupid_savedata_init_flash(&gba->memory.savedata);
    /* A guest may erase its new flash before reading any calibration byte.
     * Factory defaults must exist before that command, not replace its result. */
    const uint32_t addresses[] = {0x5555, 0x2AAA, 0x5555, 0x5555, 0x2AAA, 0x5555};
    const uint8_t commands[] = {0xAA, 0x55, 0x80, 0xAA, 0x55, 0x10};
    for (unsigned i = 0; i < 6; i++)
        cupid_memory_store8(&gba->cpu, 0x0E000000 + addresses[i], commands[i], NULL);
    CHECK(gba->cartridge.cardReader.calibrationChecked);
    CHECK(gba->memory.savedata.programming && gba->memory.savedata.data[0xD000] == 'C');
    cupid_timing_tick(&gba->timing, (int32_t)gba->memory.savedata.flashChipEraseClocks);
    CHECK(!gba->memory.savedata.programming);
    CHECK(cupid_memory_load8(&gba->cpu, 0x0E00D000, NULL) == 0xFF);
    CHECK(cupid_memory_load8(&gba->cpu, 0x0E00E000, NULL) == 0xFF);
    destroy(gba);
}

struct StrobeHistory {
    struct CupidGBA *gba;
    struct CupidTimingEvent event;
    uint16_t output;
};

static void change_between_strobes(struct CupidTiming *timing, void *context, uint32_t late) {
    (void)timing;
    (void)late;
    struct StrobeHistory *history = context;
    struct CupidCardReader *reader = &history->gba->cartridge.cardReader;
    history->output = reader->output;
    reader->sensor[0] = 0x5566;
    reader->sensor[1] = 0x7788;
}

static void strobes(void) {
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned dmaOwner = 0; dmaOwner < 2; dmaOwner++)
            for (unsigned writing = 0; writing < 2; writing++) {
                struct CupidGBA *gba = machine(epoch);
                gba->cartridge.cardReader.sensor[0] = 0x1122;
                gba->cartridge.cardReader.sensor[1] = 0x3344;
                const unsigned sample = dmaOwner ? (writing ? 10 : 9) : 6;
                struct StrobeHistory history = {.gba = gba, .output = 0xFFFF};
                history.event = (struct CupidTimingEvent){.context = &history,
                                                          .callback = change_between_strobes,
                                                          .name = "Card strobe history",
                                                          .priority = 0x60};
                cupid_timing_schedule(&gba->timing, &history.event, (int32_t)sample);
                if (dmaOwner) {
                    cupid_memory_store32(&gba->cpu, GBA_BASE_IWRAM, 0x00070005, NULL);
                    cupid_dma_write_sad(gba, 3, writing ? GBA_BASE_IWRAM : 0x0DFC0000);
                    cupid_dma_write_dad(gba, 3, writing ? 0x0DF80000 : GBA_BASE_IWRAM + 0x100);
                    cupid_dma_write_cnt_lo(gba, 3, 1);
                    cupid_dma_write_cnt(gba, 3, 0x8400);
                    cupid_timing_tick(&gba->timing, 17);
                    CHECK(gba->memory.dma[3].enable);
                    CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_IWRAM + 0x100, NULL) == 0);
                    cupid_timing_tick(&gba->timing, 1);
                    if (!writing)
                        CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_IWRAM + 0x100, NULL) ==
                              0x77881122);
                    cupid_timing_tick(&gba->timing, 1);
                    CHECK(!gba->memory.dma[3].enable && !gba->memory.dmaRunning);
                } else {
                    gba->cpu.memory.timed = true;
                    int internal = 0;
                    if (writing)
                        cupid_memory_store32(&gba->cpu, 0x0DF80000, 0x00070005, &internal);
                    else
                        CHECK(cupid_memory_load32(&gba->cpu, 0x0DFC0000, &internal) == 0x77881122);
                    CHECK(cupid_timing_global_time(&gba->timing) - epochs[epoch] == 14);
                    CHECK(internal == (writing ? 0 : 1));
                }
                CHECK(history.output == (writing ? 5 : 0));
                CHECK(gba->cartridge.cardReader.output == (writing ? 7 : 0));
                destroy(gba);
            }
}

static void prefetch(void) {
    for (unsigned epoch = 0; epoch < 4; epoch++) {
        struct CupidGBA *gba = machine(epoch);
        struct CupidCardReader *reader = &gba->cartridge.cardReader;
        reader->sensor[0] = reader->sensor[1] = 0x46C0;
        for (unsigned word = 2; word < 12; word++)
            reader->sensor[word] = (uint16_t)(0x2011 + word - 2);
        cupid_io_write(gba, REG_WAITCNT, 0x4000);
        gba->cpu.memory.timed = true;
        _CupidSetMode(&gba->cpu, MODE_THUMB);
        gba->cpu.gprs[ARM_PC] = 0x0DFC0000;
        CupidThumbWritePC(&gba->cpu);
        CHECK(gba->cpu.prefetch[0] == 0x46C0 && gba->cpu.prefetch[1] == 0x46C0);
        cupid_timing_tick(&gba->timing, 72);
        cupid_prefetch_update(gba, cupid_timing_global_time(&gba->timing));
        CHECK(gba->memory.prefetchUnit.count == 8);
        for (unsigned word = 2; word < 12; word++)
            reader->sensor[word] = 0x20EE;
        for (unsigned word = 0; word < 8; word++) {
            cupid_gba_step(gba);
            CHECK(gba->cpu.prefetch[1] == 0x2011 + word);
        }
        CHECK(gba->cpu.gprs[0] != 0xEE);
        destroy(gba);
    }
    for (unsigned a1 = 0; a1 < 2; a1++) {
        struct CupidGBA *gba = machine(0);
        struct CupidCardReader *reader = &gba->cartridge.cardReader;
        const uint16_t code[] = {0x46C0, 0x46C0, 0x805A, 0xE3A0, 0xFFFE, 0xEAFF};
        memcpy(reader->sensor, code, sizeof(code));
        cupid_io_write(gba, REG_WAITCNT, 0x4000);
        gba->cpu.memory.timed = true;
        _CupidSetMode(&gba->cpu, MODE_THUMB);
        gba->cpu.gprs[ARM_PC] = 0x0DFC0000;
        CupidThumbWritePC(&gba->cpu);
        cupid_timing_tick(&gba->timing, 9);
        cupid_prefetch_update(gba, cupid_timing_global_time(&gba->timing));
        CHECK(gba->memory.prefetchUnit.count == 1);
        reader->sensor[2] = 0x80EE;
        _CupidSetMode(&gba->cpu, MODE_ARM);
        gba->cpu.gprs[ARM_PC] = (int32_t)(0x0DFC0004 + a1 * 2);
        CupidARMWritePC(&gba->cpu);
        CHECK(gba->cpu.prefetch[0] == 0xE3A0805A && gba->cpu.prefetch[1] == 0xEAFFFFFE);
        cupid_gba_step(gba);
        CHECK(gba->cpu.gprs[8] == 0x5A);
        destroy(gba);
    }
}

static void opcodes(void) {
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned thumb = 0; thumb < 2; thumb++)
            for (unsigned a1 = 0; a1 < 2; a1++) {
                struct CupidGBA *gba = machine(epoch);
                const uint16_t words[] = {0x1122, 0x3344, 0x99AA, 0xBBCC};
                memcpy(gba->cartridge.cardReader.sensor, words, sizeof(words));
                struct StrobeHistory history = {.gba = gba};
                history.event = (struct CupidTimingEvent){.context = &history,
                                                          .callback = change_between_strobes,
                                                          .name = "Card opcode strobe",
                                                          .priority = 0x60};
                cupid_timing_schedule(&gba->timing, &history.event, 6);
                gba->cpu.memory.timed = true;
                _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
                gba->cpu.gprs[ARM_PC] = (int32_t)(0x0DFC0000 + a1 * 2);
                if (thumb)
                    CupidThumbWritePC(&gba->cpu);
                else
                    CupidARMWritePC(&gba->cpu);
                CHECK(gba->cpu.prefetch[0] == (thumb ? (a1 ? 0x3344 : 0x1122) : 0x77881122));
                CHECK(gba->cpu.prefetch[1] == (thumb ? (a1 ? 0x99AA : 0x7788) : 0xBBCC99AA));
                CHECK(cupid_timing_global_time(&gba->timing) - epochs[epoch] ==
                      (thumb ? 14u : 32u));
                destroy(gba);
            }
}

static void registers(void) {
    struct CupidGBA *gba = machine(0);
    for (unsigned value = 0; value < 0x10000; value++) {
        cupid_memory_store16(&gba->cpu, 0x0DF80000, (uint16_t)value, NULL);
        CHECK(cupid_memory_loadU16(&gba->cpu, 0x0DF9FFFE, NULL) == (value & 15));
        cupid_memory_store16(&gba->cpu, 0x0DFBFFFC, (uint16_t)value, NULL);
        CHECK(cupid_memory_loadU16(&gba->cpu, 0x0DFA0000, NULL) == ((value & 0x10A) | 4));
    }
    for (unsigned controls = 0; controls < 8; controls++) {
        cupid_card_reader_reset(&gba->cartridge.cardReader);
        cupid_memory_store8(&gba->cpu, control1, 0xFF, NULL);
        CHECK(cupid_memory_load8(&gba->cpu, control1, NULL) == 0xB0);
        pins(gba, 8 | controls << 4);
        CHECK((gba->cartridge.cardReader.control[1] & 2) == (controls == 7 ? 2 : 0));
    }
    destroy(gba);
    const uint16_t durations[] = {0, 1, 0x1FFF, 0x2000, 0x2001, 0xFFFF};
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned i = 0; i < sizeof(durations) / sizeof(durations[0]); i++) {
            gba = machine(epoch);
            cupid_memory_store8(&gba->cpu, control0 + 2, (uint8_t)durations[i], NULL);
            cupid_memory_store8(&gba->cpu, control0 + 3, (uint8_t)(durations[i] >> 8), NULL);
            CHECK(cupid_memory_load8(&gba->cpu, 0x0F00FFF2, NULL) == (durations[i] & 255));
            CHECK(cupid_memory_load8(&gba->cpu, 0x0F00FFF3, NULL) == (durations[i] >> 8));
            pins(gba, 0x78);
            const unsigned delay = durations[i] < 0x2000 ? durations[i] * 2u : 0x4000;
            if (delay) {
                cupid_timing_tick(&gba->timing, (int32_t)delay - 1);
                CHECK(!(gba->irq.pendingIF & IRQ_GAMEPAK));
                cupid_timing_tick(&gba->timing, 1);
            } else
                cupid_timing_tick(&gba->timing, 0);
            CHECK(gba->irq.pendingIF & IRQ_GAMEPAK);
            cupid_cartridge_enable_card_reader(gba, false);
            cupid_timing_tick(&gba->timing, 1);
            CHECK(gba->irqf & IRQ_GAMEPAK);
            destroy(gba);
        }
}

static void thresholds(void) {
    struct CupidGBA *gba = machine(0);
    cupid_memory_store8(&gba->cpu, 0x0E00FF80, 0xFF, NULL);
    pins(gba, 0x70);
    cupid_memory_store8(&gba->cpu, control1, 0, NULL);
    const struct CupidCardReader *reader = &gba->cartridge.cardReader;
    for (unsigned word = 0; word < 20; word++)
        CHECK(reader->sensor[word] == (word < 2 ? 0xFFFF : word == 2 ? 0x00FF : 0));
    for (unsigned block = 0; block < 48; block++)
        CHECK(reader->sensor[20 + block] == (block < 8 ? 127 : 0));
    CHECK(reader->sensor[68] == 127);
    CHECK(!cupid_timing_is_scheduled(&gba->timing, &reader->irqEvent));
    CHECK(!(gba->irq.pendingIF & IRQ_GAMEPAK));
    destroy(gba);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "serial"))
        RUN(serial);
    else if (!strcmp(argv[1], "ports"))
        RUN(ports);
    else if (!strcmp(argv[1], "scan"))
        RUN(scan);
    else if (!strcmp(argv[1], "dma"))
        RUN(dma);
    else if (!strcmp(argv[1], "lifecycle"))
        RUN(lifecycle);
    else if (!strcmp(argv[1], "calibration"))
        RUN(calibration);
    else if (!strcmp(argv[1], "strobes"))
        RUN(strobes);
    else if (!strcmp(argv[1], "prefetch"))
        RUN(prefetch);
    else if (!strcmp(argv[1], "thresholds"))
        RUN(thresholds);
    else if (!strcmp(argv[1], "opcodes"))
        RUN(opcodes);
    else if (!strcmp(argv[1], "registers"))
        RUN(registers);
    else
        CHECK(false);
    return 0;
}
