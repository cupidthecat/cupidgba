/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "cupid/platform/peripherals.h"
#include <errno.h>
#include <inttypes.h>

static bool number(const char* text, uint64_t* value) {
    char* end;
    errno = 0;
    *value = strtoull(text, &end, 0);
    return !errno && text[0] && text[0] != '-' && !*end;
}

static bool screenshot(const struct CupidGBA* gba, const char* path) {
    FILE* file = fopen(path, "wb");
    if (!file) return false;
    fprintf(file, "P6\n240 160\n255\n");
    for (unsigned i = 0; i < 240 * 160; i++) {
        uint32_t pixel = gba->video.outputBuffer[i];
        uint8_t rgb[] = {pixel & 255, (pixel >> 8) & 255, (pixel >> 16) & 255};
        if (fwrite(rgb, 1, sizeof(rgb), file) != sizeof(rgb)) { fclose(file); return false; }
    }
    return fclose(file) == 0;
}

struct AudioTrace { FILE* file; bool failed; };
struct MotorTrace { FILE* file; bool failed; };

static void motor_output(void* context, bool active, uint64_t clock) {
    struct MotorTrace* trace = context;
    uint8_t record[12] = {0};
    for (unsigned i = 0; i < 8; i++) record[i] = (uint8_t)(clock >> (i * 8));
    record[8] = active;
    if (fwrite(record, 1, sizeof(record), trace->file) != sizeof(record)) trace->failed = true;
}

static void audio_sample(void* context, uint64_t clock, int16_t left, int16_t right) {
    struct AudioTrace* trace = context;
    uint8_t record[12];
    for (unsigned i = 0; i < 8; i++) record[i] = (uint8_t)(clock >> (i * 8));
    record[8] = (uint8_t)left;
    record[9] = (uint8_t)((uint16_t)left >> 8);
    record[10] = (uint8_t)right;
    record[11] = (uint8_t)((uint16_t)right >> 8);
    if (fwrite(record, 1, sizeof(record), trace->file) != sizeof(record)) trace->failed = true;
}

int main(int argc, char** argv) {
    struct KeyEvent { uint64_t frame; uint16_t keys; } keyEvents[64];
    unsigned keyEventCount = 0, nextKeyEvent = 0;
    const char* rom = NULL;
    const char* bios = NULL;
    const char* output = NULL;
    const char* vramOutput = NULL;
    const char* ioOutput = NULL;
    const char* ewramOutput = NULL;
    const char* audioOutput = NULL;
    const char* motorOutput = NULL;
    struct AudioTrace audioTrace = {0};
    struct MotorTrace motorTrace = {0};
    uint64_t frames = 120, cycleLimit = UINT64_C(280896) * 120, stepLimit = 50000000;
    uint64_t terminalAddress = 0;
    uint64_t startAddress = 0;
    int expectedRegister = -1;
    bool sstFlash = false;
    struct CupidPlatformPeripherals peripherals = CUPID_PLATFORM_PERIPHERALS_DEFAULT;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bios") && i + 1 < argc) bios = argv[++i];
        else if (!strcmp(argv[i], "--flash-profile") && i + 1 < argc) {
            if (strcmp(argv[++i], "sst39vf512")) return 2;
            sstFlash = true;
        }
        else if ((!strcmp(argv[i], "--solar") || !strcmp(argv[i], "--rumble") ||
                  !strcmp(argv[i], "--gyro") || !strcmp(argv[i], "--tilt")) && i + 1 < argc) {
            int* presence = !strcmp(argv[i], "--solar") ? &peripherals.solarPresence :
                !strcmp(argv[i], "--rumble") ? &peripherals.rumblePresence :
                !strcmp(argv[i], "--gyro") ? &peripherals.gyroPresence : &peripherals.tiltPresence;
            if (!cupid_platform_peripheral_mode(argv[++i], presence)) return 2;
        } else if (!strcmp(argv[i], "--sunlight") && i + 1 < argc) {
            if (!cupid_platform_solar_level(argv[++i], &peripherals.solarLevel)) return 2;
        } else if (!strcmp(argv[i], "--gyro-sample") && i + 1 < argc) {
            if (!cupid_platform_gyro_sample(argv[++i], &peripherals.gyroSample)) return 2;
        } else if ((!strcmp(argv[i], "--tilt-x") || !strcmp(argv[i], "--tilt-y")) && i + 1 < argc) {
            uint16_t* axis = !strcmp(argv[i], "--tilt-x") ? &peripherals.tiltX : &peripherals.tiltY;
            if (!cupid_platform_sample12(argv[++i], axis)) return 2;
        }
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) output = argv[++i];
        else if (!strcmp(argv[i], "--dump-vram") && i + 1 < argc) vramOutput = argv[++i];
        else if (!strcmp(argv[i], "--dump-io") && i + 1 < argc) ioOutput = argv[++i];
        else if (!strcmp(argv[i], "--dump-ewram") && i + 1 < argc) ewramOutput = argv[++i];
        else if (!strcmp(argv[i], "--dump-audio") && i + 1 < argc) audioOutput = argv[++i];
        else if (!strcmp(argv[i], "--dump-rumble") && i + 1 < argc) motorOutput = argv[++i];
        else if (!strcmp(argv[i], "--terminal-address") && i + 1 < argc) {
            if (!number(argv[++i], &terminalAddress) || terminalAddress > UINT32_MAX) return 2;
        }
        else if (!strcmp(argv[i], "--start-address") && i + 1 < argc) {
            if (!number(argv[++i], &startAddress) || startAddress < GBA_BASE_ROM0 ||
                startAddress >= GBA_BASE_SRAM || (startAddress & 3)) return 2;
        }
        else if (!strcmp(argv[i], "--key-event") && i + 2 < argc) {
            uint64_t frame, keys;
            if (keyEventCount == 64 || !number(argv[++i], &frame) ||
                !number(argv[++i], &keys) || keys > 0x3FF ||
                (keyEventCount && frame < keyEvents[keyEventCount - 1].frame)) return 2;
            keyEvents[keyEventCount++] = (struct KeyEvent){frame, (uint16_t)keys};
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            if (!number(argv[++i], &frames) || !frames || frames > 100000) return 2;
            cycleLimit = frames * UINT64_C(280896) + UINT64_C(280896) * 300;
        } else if (!strcmp(argv[i], "--steps") && i + 1 < argc) {
            if (!number(argv[++i], &stepLimit) || !stepLimit) return 2;
        } else if (!strcmp(argv[i], "--expect-loop-register") && i + 1 < argc) {
            uint64_t index;
            if (!number(argv[++i], &index) || index > 14) return 2;
            expectedRegister = (int)index;
        } else if (argv[i][0] != '-' && !rom) rom = argv[i];
        else {
            fprintf(stderr, "Usage: %s ROM [--bios FILE] [--flash-profile sst39vf512] [--frames N] [--steps N] [--key-event FRAME MASK] [--screenshot FILE.ppm] [--dump-vram FILE] [--dump-io FILE] [--dump-ewram FILE] [--dump-audio FILE] [--dump-rumble FILE] [--solar auto|on|off] [--sunlight 0..255] [--rumble auto|on|off] [--gyro auto|on|off] [--gyro-sample 0..4095] [--tilt auto|on|off] [--tilt-x 0..4095] [--tilt-y 0..4095] [--start-address ADDRESS] [--expect-loop-register N] [--terminal-address ADDRESS]\n", argv[0]);
            return 2;
        }
    }
    if (!rom) return 2;
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    if (!gba) return 2;
    cupid_gba_create(gba);
    int result = 2;
    if (!cupid_load_rom(gba, rom) || (bios && !cupid_load_bios(gba, bios))) goto cleanup;
    if (sstFlash) {
        cupid_savedata_force_type(&gba->memory.savedata, SAVE_FLASH512);
        if (!cupid_savedata_set_flash_profile(&gba->memory.savedata, FLASH_PROFILE_SST39VF512)) goto cleanup;
    }
    cupid_platform_peripherals_configure(gba, &peripherals);
    cupid_gba_reset(gba);
    /* An explicit probe entry exercises guest code with the selected BIOS
     * mapped. It does not execute or qualify the BIOS boot sequence. */
    if (startAddress) {
        gba->cpu.cpsr.t = 0;
        _CupidSetMode(&gba->cpu, MODE_ARM);
        gba->cpu.gprs[ARM_PC] = (int32_t)startAddress;
        CupidARMWritePC(&gba->cpu);
    }
    if (motorOutput) {
        motorTrace.file = fopen(motorOutput, "wb");
        if (!motorTrace.file) { perror(motorOutput); goto cleanup; }
        if (fwrite("CPDRMB01", 1, 8, motorTrace.file) != 8) goto cleanup;
        cupid_cartridge_set_rumble_output(gba, motor_output, &motorTrace);
    }
    if (audioOutput) {
        audioTrace.file = fopen(audioOutput, "wb");
        if (!audioTrace.file) { perror(audioOutput); goto cleanup; }
        if (fwrite("CPDPCM01", 1, 8, audioTrace.file) != 8) goto cleanup;
        gba->audio.sampleSink = audio_sample;
        gba->audio.sampleSinkContext = &audioTrace;
    }
    uint64_t completed = 0, steps = 0, stoppedClocks = 0;
    bool terminal = false;
    while (completed < frames && steps < stepLimit &&
           cupid_timing_global_time(&gba->timing) + stoppedClocks < cycleLimit) {
        if (expectedRegister >= 0 && gba->cpu.executionMode == MODE_ARM &&
            (uint32_t)gba->cpu.gprs[ARM_PC] >= GBA_BASE_ROM0 &&
            (!terminalAddress || (uint32_t)gba->cpu.gprs[ARM_PC] - 4 == terminalAddress) &&
            gba->cpu.prefetch[0] == UINT32_C(0xEAFFFFFE)) {
            terminal = true;
            break;
        }
        while (nextKeyEvent < keyEventCount && keyEvents[nextKeyEvent].frame <= completed) {
            gba->keysActive = keyEvents[nextKeyEvent++].keys;
            cupid_gba_test_keypad_irq(gba);
        }
        if (gba->stopped) {
            uint64_t clocks = cycleLimit - cupid_timing_global_time(&gba->timing) - stoppedClocks;
            if (clocks > VIDEO_TOTAL_LENGTH) clocks = VIDEO_TOTAL_LENGTH;
            stoppedClocks += cupid_gba_advance_stopped_time(gba, clocks);
        } else cupid_gba_step(gba);
        steps++;
        if (gba->frameReady) { completed++; gba->frameReady = false; }
    }
    printf("frames=%" PRIu64 " cycles=%" PRIu64 " steps=%" PRIu64 " pc=%08" PRIx32 "\n",
           completed, cupid_timing_global_time(&gba->timing), steps, (uint32_t)gba->cpu.gprs[ARM_PC]);
    if (stoppedClocks) printf("stopped_cycles=%" PRIu64 "\n", stoppedClocks);
    if (expectedRegister >= 0) {
        uint32_t value = (uint32_t)gba->cpu.gprs[expectedRegister];
        printf("terminal=%s r%d=%" PRIu32 " result=%s\n", terminal ? "yes" : "no", expectedRegister,
               value, terminal ? (value ? "FAIL" : "PASS") : "TIMEOUT");
        result = terminal && !value ? 0 : 1;
    } else result = completed == frames ? 0 : 1;
    if (output && !screenshot(gba, output)) { perror(output); result = 2; }
    const char* dumpPaths[] = {vramOutput, ioOutput, ewramOutput};
    const void* dumpData[] = {gba->video.vram, gba->memory.io, gba->memory.ewram};
    const size_t dumpSizes[] = {GBA_SIZE_VRAM, sizeof(gba->memory.io), GBA_SIZE_EWRAM};
    for (unsigned i = 0; i < 3; i++) {
        if (!dumpPaths[i]) continue;
        FILE* file = fopen(dumpPaths[i], "wb");
        if (!file) { perror(dumpPaths[i]); result = 2; continue; }
        bool written = fwrite(dumpData[i], 1, dumpSizes[i], file) == dumpSizes[i];
        if (fclose(file) != 0 || !written) result = 2;
    }
cleanup:
    cupid_cartridge_set_rumble_output(gba, NULL, NULL);
    if (motorTrace.file && (fclose(motorTrace.file) != 0 || motorTrace.failed)) result = 2;
    if (audioTrace.file && (fclose(audioTrace.file) != 0 || audioTrace.failed)) result = 2;
    cupid_gba_destroy(gba);
    free(gba);
    return result;
}
