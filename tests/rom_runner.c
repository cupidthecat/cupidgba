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

struct AudioTrace { FILE* file; bool failed; };

enum { MAX_REGISTER_OBSERVATIONS = 128 };
struct RegisterObservation { uint32_t address, expected; unsigned reg; };
struct RegisterObservations {
    struct RegisterObservation entries[MAX_REGISTER_OBSERVATIONS];
    unsigned count, next;
    bool failed, orderFailed;
};

static void observe_instruction(const struct CupidARM* cpu, void* context) {
    struct RegisterObservations* observations = context;
    const bool thumb = cpu->executionMode == MODE_THUMB;
    const uint32_t pc = cupid_arm_instruction_address(cpu);
    bool watched = false;
    for (unsigned i = 0; i < observations->count; i++) {
        if (observations->entries[i].address == pc) { watched = true; break; }
    }
    if (!watched) return;
    /* Only actual instruction issue reaches this hook. A HALT poll, IRQ
     * arbitration or discarded exception fetch cannot consume an expectation. */
    if (observations->next == observations->count || observations->entries[observations->next].address != pc) {
        if (!observations->orderFailed)
            printf("observation-order=FAIL pc=%08" PRIx32 " next=%u\n", pc, observations->next);
        observations->orderFailed = true;
        return;
    }
    const struct RegisterObservation* observation = &observations->entries[observations->next];
    const uint32_t value = (uint32_t)cpu->gprs[observation->reg];
    printf("observation=%u pc=%08" PRIx32 " mode=%s opcode=%08" PRIx32
           " r%u=%08" PRIx32 " expected=%08" PRIx32 " result=%s\n",
           observations->next, pc, thumb ? "thumb" : "arm", cpu->prefetch[0],
           observation->reg, value, observation->expected,
           value == observation->expected ? "PASS" : "FAIL");
    if (value != observation->expected) observations->failed = true;
    observations->next++;
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
    struct RegisterObservations observations = {0};
    const char* rom = NULL;
    const char* bios = NULL;
    const char* output = NULL;
    const char* vramOutput = NULL;
    const char* ioOutput = NULL;
    const char* ewramOutput = NULL;
    const char* saveOutput = NULL;
    const char* audioOutput = NULL;
    struct AudioTrace audioTrace = {0};
    uint64_t frames = 120, cycleLimit = UINT64_C(280896) * 120, stepLimit = 50000000;
    uint64_t terminalAddress = 0;
    uint64_t checkpointAddress = 0;
    int expectedRegister = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bios") && i + 1 < argc) bios = argv[++i];
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) output = argv[++i];
        else if (!strcmp(argv[i], "--dump-vram") && i + 1 < argc) vramOutput = argv[++i];
        else if (!strcmp(argv[i], "--dump-io") && i + 1 < argc) ioOutput = argv[++i];
        else if (!strcmp(argv[i], "--dump-ewram") && i + 1 < argc) ewramOutput = argv[++i];
        else if (!strcmp(argv[i], "--dump-save") && i + 1 < argc) saveOutput = argv[++i];
        else if (!strcmp(argv[i], "--dump-audio") && i + 1 < argc) audioOutput = argv[++i];
        else if (!strcmp(argv[i], "--terminal-address") && i + 1 < argc) {
            if (!number(argv[++i], &terminalAddress) || terminalAddress > UINT32_MAX) return 2;
        }
        else if (!strcmp(argv[i], "--checkpoint-address") && i + 1 < argc) {
            if (!number(argv[++i], &checkpointAddress) || !checkpointAddress || checkpointAddress > UINT32_MAX) return 2;
        }
        else if (!strcmp(argv[i], "--expect-register-at") && i + 3 < argc) {
            uint64_t address, reg, expected;
            if (observations.count == MAX_REGISTER_OBSERVATIONS ||
                !number(argv[++i], &address) || address > UINT32_MAX ||
                !number(argv[++i], &reg) || reg > 15 ||
                !number(argv[++i], &expected) || expected > UINT32_MAX) return 2;
            observations.entries[observations.count++] = (struct RegisterObservation){
                (uint32_t)address, (uint32_t)expected, (unsigned)reg};
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
            fprintf(stderr, "Usage: %s ROM [--bios FILE] [--frames N] [--steps N] [--key-event FRAME MASK] [--screenshot FILE.ppm] [--dump-vram FILE] [--dump-io FILE] [--dump-ewram FILE] [--dump-save FILE] [--dump-audio FILE] [--expect-loop-register N] [--terminal-address ADDRESS] [--checkpoint-address ADDRESS] [--expect-register-at ADDRESS REGISTER VALUE]\n", argv[0]);
            return 2;
        }
    }
    if (!rom) return 2;
    if (checkpointAddress && expectedRegister < 0) return 2;
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    if (!gba) return 2;
    cupid_gba_create(gba);
    int result = 2;
    if (!cupid_load_rom(gba, rom) || (bios && !cupid_load_bios(gba, bios))) goto cleanup;
    cupid_gba_reset(gba);
    if (observations.count) {
        gba->cpu.instructionObserver = observe_instruction;
        gba->cpu.instructionObserverContext = &observations;
    }
    if (audioOutput) {
        audioTrace.file = fopen(audioOutput, "wb");
        if (!audioTrace.file) { perror(audioOutput); goto cleanup; }
        if (fwrite("CPDPCM01", 1, 8, audioTrace.file) != 8) goto cleanup;
        gba->audio.sampleSink = audio_sample;
        gba->audio.sampleSinkContext = &audioTrace;
    }
    uint64_t completed = 0, steps = 0;
    bool terminal = false;
    bool checkpoint = false;
    uint32_t checkpointValue = 0;
    while (completed < frames && steps < stepLimit &&
           cupid_timing_global_time(&gba->timing) < cycleLimit) {
        if (checkpointAddress && !checkpoint && gba->cpu.executionMode == MODE_ARM &&
            cupid_arm_instruction_address(&gba->cpu) == checkpointAddress) {
            checkpoint = true;
            checkpointValue = (uint32_t)gba->cpu.gprs[expectedRegister];
        }
        if (expectedRegister >= 0 && gba->cpu.executionMode == MODE_ARM &&
            (uint32_t)gba->cpu.gprs[ARM_PC] >= GBA_BASE_ROM0 &&
            (!terminalAddress || cupid_arm_instruction_address(&gba->cpu) == terminalAddress) &&
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
        if (checkpointAddress) printf("checkpoint=%s r%d=%" PRIu32 " result=%s\n",
            checkpoint ? "yes" : "no", expectedRegister, checkpointValue,
            checkpoint ? (checkpointValue ? "FAIL" : "PASS") : "MISSING");
        result = terminal && !value && (!checkpointAddress || (checkpoint && !checkpointValue)) ? 0 : 1;
    } else result = completed == frames ? 0 : 1;
    if (observations.count) {
        const bool failed = observations.failed || observations.orderFailed;
        printf("observations=%u/%u result=%s\n", observations.next, observations.count,
               failed ? "FAIL" : observations.next != observations.count ? "MISSING" : "PASS");
        if (failed || observations.next != observations.count) result = 1;
    }
    if (output && !screenshot(gba, output)) { perror(output); result = 2; }
    const char* dumpPaths[] = {vramOutput, ioOutput, ewramOutput, saveOutput};
    const void* dumpData[] = {gba->video.vram, gba->memory.io, gba->memory.ewram, gba->memory.savedata.data};
    const size_t dumpSizes[] = {GBA_SIZE_VRAM, sizeof(gba->memory.io), GBA_SIZE_EWRAM, gba->memory.savedata.size};
    for (unsigned i = 0; i < 4; i++) {
        if (!dumpPaths[i]) continue;
        if (!dumpData[i] || !dumpSizes[i]) { fprintf(stderr, "No data for requested snapshot\n"); result = 2; continue; }
        FILE* file = fopen(dumpPaths[i], "wb");
        if (!file) { perror(dumpPaths[i]); result = 2; continue; }
        bool written = fwrite(dumpData[i], 1, dumpSizes[i], file) == dumpSizes[i];
        if (fclose(file) != 0 || !written) result = 2;
    }
cleanup:
    if (audioTrace.file && (fclose(audioTrace.file) != 0 || audioTrace.failed)) result = 2;
    cupid_gba_destroy(gba);
    free(gba);
    return result;
}
