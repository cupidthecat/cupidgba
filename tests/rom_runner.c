/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
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

int main(int argc, char** argv) {
    struct KeyEvent { uint64_t frame; uint16_t keys; } keyEvents[64];
    unsigned keyEventCount = 0, nextKeyEvent = 0;
    const char* rom = NULL;
    const char* bios = NULL;
    const char* output = NULL;
    const char* vramOutput = NULL;
    const char* ioOutput = NULL;
    const char* ewramOutput = NULL;
    uint64_t frames = 120, cycleLimit = UINT64_C(280896) * 120, stepLimit = 50000000;
    uint64_t terminalAddress = 0;
    int expectedRegister = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bios") && i + 1 < argc) bios = argv[++i];
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) output = argv[++i];
        else if (!strcmp(argv[i], "--dump-vram") && i + 1 < argc) vramOutput = argv[++i];
        else if (!strcmp(argv[i], "--dump-io") && i + 1 < argc) ioOutput = argv[++i];
        else if (!strcmp(argv[i], "--dump-ewram") && i + 1 < argc) ewramOutput = argv[++i];
        else if (!strcmp(argv[i], "--terminal-address") && i + 1 < argc) {
            if (!number(argv[++i], &terminalAddress) || terminalAddress > UINT32_MAX) return 2;
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
            fprintf(stderr, "Usage: %s ROM [--bios FILE] [--frames N] [--steps N] [--key-event FRAME MASK] [--screenshot FILE.ppm] [--dump-vram FILE] [--dump-io FILE] [--dump-ewram FILE] [--expect-loop-register N] [--terminal-address ADDRESS]\n", argv[0]);
            return 2;
        }
    }
    if (!rom) return 2;
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    if (!gba) return 2;
    cupid_gba_create(gba);
    int result = 2;
    if (!cupid_load_rom(gba, rom) || (bios && !cupid_load_bios(gba, bios))) goto cleanup;
    cupid_gba_reset(gba);
    uint64_t completed = 0, steps = 0;
    bool terminal = false;
    while (completed < frames && steps < stepLimit &&
           cupid_timing_global_time(&gba->timing) < cycleLimit) {
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
        cupid_gba_step(gba);
        steps++;
        if (gba->frameReady) { completed++; gba->frameReady = false; }
    }
    printf("frames=%" PRIu64 " cycles=%" PRIu64 " steps=%" PRIu64 " pc=%08" PRIx32 "\n",
           completed, cupid_timing_global_time(&gba->timing), steps, (uint32_t)gba->cpu.gprs[ARM_PC]);
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
    cupid_gba_destroy(gba);
    free(gba);
    return result;
}
