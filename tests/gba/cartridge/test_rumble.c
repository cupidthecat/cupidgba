/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../../test.h"

struct MotorTrace { bool active[1024]; uint64_t clock[1024]; unsigned count; };
static void motor(void* context, bool active, uint64_t clock) {
    struct MotorTrace* trace = context;
    CHECK(trace->count < 1024);
    trace->active[trace->count] = active;
    trace->clock[trace->count++] = clock;
}

static struct CupidGBA* machine(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    gba->memory.rom = calloc(1, 0x4000);
    CHECK(gba->memory.rom);
    gba->memory.romSize = 0x4000;
    gba->memory.romMask = 0x3FFF;
    cupid_gba_reset(gba);
    return gba;
}
static void destroy(struct CupidGBA* gba) { cupid_gba_destroy(gba); free(gba); }

static void port_directions_repeated_writes_and_read_control(void) {
    struct CupidGBA* gba = machine();
    struct MotorTrace trace = {0};
    cupid_cartridge_enable_rumble(gba, true);
    cupid_cartridge_set_rumble_output(gba, motor, &trace);
    CHECK(trace.count == 1 && !trace.active[0]);
    for (unsigned readable = 0; readable < 2; readable++) {
        cupid_memory_store16(&gba->cpu, 0x080000C8, (int16_t)readable, NULL);
        for (unsigned direction = 0; direction < 16; direction++) {
            cupid_memory_store16(&gba->cpu, 0x080000C6, (int16_t)direction, NULL);
            for (unsigned output = 0; output < 16; output++) {
                cupid_timing_tick(&gba->timing, 3);
                cupid_memory_store16(&gba->cpu, 0x080000C4, (int16_t)output, NULL);
                CHECK(gba->cartridge.rumbleActive == ((output & direction & 8) != 0));
                CHECK(trace.active[trace.count - 1] == gba->cartridge.rumbleActive);
                const unsigned previous = trace.count;
                cupid_memory_store16(&gba->cpu, 0x080000C4, (int16_t)output, NULL);
                CHECK(trace.count == previous);
                const bool wasActive = gba->cartridge.rumbleActive;
                cupid_memory_store8(&gba->cpu, 0x080000C4, 0, NULL);
                CHECK(trace.count == previous + (wasActive ? 1u : 0u));
                CHECK(!gba->cartridge.rumbleActive && !trace.active[trace.count - 1]);
                if (wasActive) CHECK(trace.clock[trace.count - 1] == cupid_timing_global_time(&gba->timing));
            }
        }
    }
    cupid_memory_store16(&gba->cpu, 0x080000C4, 8, NULL);
    cupid_cartridge_enable_rumble(gba, false);
    CHECK(!gba->cartridge.rumbleActive && !trace.active[trace.count - 1]);
    const unsigned count = trace.count;
    cupid_memory_store16(&gba->cpu, 0x080000C6, 8, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C4, 8, NULL);
    CHECK(trace.count == count);
    destroy(gba);
}

static void reset_replacement_subscription_and_destroy_stop_output(void) {
    struct CupidGBA* gba = machine();
    struct MotorTrace first = {0}, second = {0};
    cupid_cartridge_set_rumble_output(gba, motor, &first);
    cupid_cartridge_enable_rumble(gba, true);
    cupid_memory_store16(&gba->cpu, 0x080000C6, 8, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C4, 8, NULL);
    CHECK(first.count == 2 && first.active[1]);
    cupid_cartridge_set_rumble_output(gba, motor, &second);
    CHECK(first.count == 3 && !first.active[2]);
    CHECK(second.count == 1 && second.active[0]);
    cupid_cartridge_set_rumble_output(gba, NULL, NULL);
    CHECK(second.count == 2 && !second.active[1] && gba->cartridge.rumbleActive);
    cupid_cartridge_set_rumble_output(gba, motor, &second);
    CHECK(second.count == 3 && second.active[2]);
    cupid_gba_reset(gba);
    CHECK(second.count == 4 && !second.active[3] && gba->cartridge.rumblePresent);
    cupid_memory_store16(&gba->cpu, 0x080000C6, 8, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C4, 8, NULL);
    cupid_cartridge_set_solar_level(gba, 73);
    memcpy(gba->memory.rom + 0xAC, "V49E", 4);
    cupid_cartridge_attach(gba, gba->memory.rom, gba->memory.romSize);
    CHECK(!second.active[second.count - 1] && !gba->cartridge.rumbleActive);
    CHECK(gba->cartridge.rumblePresent && gba->cartridge.rumbleChanged == motor);
    CHECK(gba->cartridge.solar.threshold == 182);
    cupid_memory_store16(&gba->cpu, 0x080000C6, 8, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C4, 8, NULL);
    const unsigned count = second.count;
    destroy(gba);
    CHECK(second.count == count + 1 && !second.active[count]);
}

static void documented_board_detection_and_bounds(void) {
    const char* solar[] = {"U3IJ", "U3IE", "U3IP", "U32J", "U32E", "U32P", "U33J"};
    const char* rumble[] = {"V49J", "V49E", "V49P", "2GBP", "RZWJ", "RZWE", "RZWP"};
    struct CupidGBA* gba = machine();
    for (unsigned i = 0; i < 7; i++) {
        memcpy(gba->memory.rom + 0xAC, solar[i], 4);
        cupid_cartridge_attach(gba, gba->memory.rom, gba->memory.romSize);
        CHECK(gba->cartridge.solarPresent && gba->cartridge.rtcPresent && !gba->cartridge.rumblePresent);
        memcpy(gba->memory.rom + 0xAC, rumble[i], 4);
        cupid_cartridge_attach(gba, gba->memory.rom, gba->memory.romSize);
        CHECK(gba->cartridge.rumblePresent && !gba->cartridge.solarPresent && !gba->cartridge.rtcPresent);
    }
    memcpy(gba->memory.rom + 0xAC, "U33E", 4);
    cupid_cartridge_attach(gba, gba->memory.rom, gba->memory.romSize);
    CHECK(!gba->cartridge.solarPresent && !gba->cartridge.rumblePresent);
    memcpy(gba->memory.rom + 0xAC, "U3IJ", 4);
    cupid_cartridge_attach(gba, gba->memory.rom, 0xAF);
    CHECK(!gba->cartridge.solarPresent && !gba->cartridge.rtcPresent);
    cupid_cartridge_attach(gba, gba->memory.rom, 0xB0);
    CHECK(gba->cartridge.solarPresent && gba->cartridge.rtcPresent);
    destroy(gba);
}

static void timed_word_and_dma_motor_transitions(void) {
    for (unsigned dma = 0; dma < 2; dma++) {
        for (unsigned wrap = 0; wrap < 2; wrap++) {
            struct CupidGBA* gba = machine();
            struct MotorTrace trace = {0};
            cupid_cartridge_enable_rumble(gba, true);
            cupid_cartridge_set_rumble_output(gba, motor, &trace);
            cupid_memory_store16(&gba->cpu, 0x080000C6, 15, NULL);
            cupid_timing_clear(&gba->timing);
            const uint64_t base = wrap ? UINT32_MAX - 4u : 0;
            gba->timing.globalCycles = base;
            gba->timing.masterCycles = (uint32_t)base;
            trace.count = 0;
            if (dma) {
                cupid_memory_store32(&gba->cpu, GBA_BASE_IWRAM, 8, NULL);
                cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM);
                cupid_dma_write_dad(gba, 3, 0x080000C4);
                cupid_dma_write_cnt_lo(gba, 3, 1);
                cupid_dma_write_cnt(gba, 3, 0xC400);
                cupid_timing_tick(&gba->timing, 32);
                CHECK(gba->irqf & IRQ_DMA3);
            } else {
                gba->cpu.memory.timed = true;
                int clocks = 0;
                cupid_memory_store32(&gba->cpu, 0x080000C4, 8, &clocks);
                CHECK(clocks == 0);
            }
            CHECK(trace.count == 2 && trace.active[0] && !trace.active[1]);
            /* ROM strobes take 4+1 then 2+1 clocks. DMA adds its two-clock
             * startup and a two-clock IWRAM word read before those strobes. */
            CHECK(trace.clock[0] == base + (dma ? 9 : 5));
            CHECK(trace.clock[1] == base + (dma ? 12 : 8));
            destroy(gba);
        }
    }
}

static void failed_load_preserves_cartridge_and_success_rebinds_cache(const char* directory) {
    char emptyPath[1024], validPath[1024], missingPath[1024];
    const int emptySize = snprintf(emptyPath, sizeof(emptyPath), "%s/rumble-empty.generated.gba", directory);
    const int validSize = snprintf(validPath, sizeof(validPath), "%s/rumble-valid.generated.gba", directory);
    const int missingSize = snprintf(missingPath, sizeof(missingPath), "%s/rumble-absent.generated.gba", directory);
    CHECK(emptySize > 0 && (size_t)emptySize < sizeof(emptyPath));
    CHECK(validSize > 0 && (size_t)validSize < sizeof(validPath));
    CHECK(missingSize > 0 && (size_t)missingSize < sizeof(missingPath));
    FILE* file = fopen(emptyPath, "wb");
    CHECK(file && fclose(file) == 0);
    uint8_t image[1024] = {0};
    memcpy(image + 0xAC, "V49E", 4);
    const uint32_t nop = 0xE1A00000;
    memcpy(image + 0x200, &nop, sizeof(nop));
    file = fopen(validPath, "wb");
    CHECK(file && fwrite(image, 1, sizeof(image), file) == sizeof(image) && fclose(file) == 0);
    struct CupidGBA* gba = machine();
    struct MotorTrace trace = {0};
    cupid_cartridge_enable_rumble(gba, true);
    cupid_cartridge_set_rumble_output(gba, motor, &trace);
    cupid_memory_store16(&gba->cpu, 0x080000C6, 8, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C4, 8, NULL);
    cupid_cartridge_set_solar_level(gba, 73);
    cupid_savedata_force_type(&gba->memory.savedata, SAVE_SRAM);
    gba->memory.savedata.data[7] = 31;
    gba->memory.savedata.dirty = true;
    gba->cpu.gprs[ARM_PC] = 0x08000200;
    gba->cpu.memory.setActiveRegion(&gba->cpu, 0x08000200);
    const uint8_t* previous = gba->memory.rom;
    const uintptr_t previousAddress = (uintptr_t)previous;
    const uint32_t* active = gba->cpu.memory.activeRegion;
    const uint64_t identity = gba->cartridge.identity;
    const unsigned count = trace.count;
    CHECK(!cupid_load_rom(gba, emptyPath));
    CHECK(gba->memory.rom && gba->memory.rom == previous && gba->memory.romSize == 0x4000 && gba->memory.romMask == 0x3FFF);
    CHECK(gba->cpu.memory.activeRegion == active && gba->cartridge.identity == identity);
    CHECK(trace.count == count && gba->cartridge.rumbleActive && gba->cartridge.direction == 8);
    CHECK(gba->memory.savedata.data[7] == 31 && gba->memory.savedata.dirty);
    CHECK(!cupid_load_rom(gba, missingPath));
    CHECK(gba->memory.rom == previous && trace.count == count && gba->cartridge.rumbleActive);
    CHECK(cupid_load_rom(gba, validPath));
    CHECK((uintptr_t)gba->memory.rom != previousAddress && gba->memory.romSize == sizeof(image) && gba->memory.romMask == 1023);
    CHECK(gba->cpu.memory.activeRegion == (const uint32_t*)gba->memory.rom);
    CHECK(cupid_memory_load32(&gba->cpu, 0x08000200, NULL) == nop);
    CHECK(trace.count == count + 1 && !trace.active[count] && !gba->cartridge.rumbleActive);
    CHECK(gba->cartridge.rumblePresent && gba->cartridge.rumbleChanged == motor);
    CHECK(!gba->cartridge.direction && gba->cartridge.solar.threshold == 182);
    destroy(gba);
    CHECK(remove(emptyPath) == 0 && remove(validPath) == 0);
    puts("PASS failed_load_preserves_cartridge_and_success_rebinds_cache");
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    RUN(port_directions_repeated_writes_and_read_control);
    RUN(reset_replacement_subscription_and_destroy_stop_output);
    RUN(documented_board_detection_and_bounds);
    RUN(timed_word_and_dma_motor_transitions);
    failed_load_preserves_cartridge_and_success_rebinds_cache(argv[1]);
    return 0;
}
