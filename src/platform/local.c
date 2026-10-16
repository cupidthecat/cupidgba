/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "local.h"
#include "cupid/cable.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool cupid_platform_local_save_path(const char* rom, unsigned player, char* out, size_t capacity) {
    if (!rom || !out || player >= 4) return false;
    const char* slash = strrchr(rom, '/');
#ifdef _WIN32
    const char* backslash = strrchr(rom, '\\');
    if (backslash && (!slash || backslash > slash)) slash = backslash;
#endif
    const char* dot = strrchr(rom, '.');
    size_t prefix = dot && (!slash || dot > slash) ? (size_t)(dot - rom) : strlen(rom);
    if (capacity < 8 || prefix > capacity - 8) return false;
    memcpy(out, rom, prefix);
    return snprintf(out + prefix, capacity - prefix, ".p%u.sav", player + 1) == 7;
}

bool cupid_platform_render_local(struct CupidPlatform* platform, SDL_Texture* const textures[],
                                 struct CupidGBA* const cores[], unsigned count, unsigned focus) {
    if (!platform || !textures || !cores || count < 2 || count > 4 || focus >= count) return false;
    int width, height;
    if (SDL_GetRendererOutputSize(platform->renderer, &width, &height)) return false;
    SDL_SetRenderDrawColor(platform->renderer, 0, 0, 0, 255);
    if (SDL_RenderClear(platform->renderer)) return false;
    const int columns = 2, rows = count > 2 ? 2 : 1;
    for (unsigned i = 0; i < count; ++i) {
        if (SDL_UpdateTexture(textures[i], NULL, cores[i]->video.outputBuffer, GBA_SCREEN_WIDTH * 4)) return false;
        SDL_Rect area = {(int)(i % 2) * width / columns, (int)(i / 2) * height / rows, width / columns, height / rows};
        if (SDL_RenderCopy(platform->renderer, textures[i], NULL, &area)) return false;
        if (i == focus) {
            SDL_SetRenderDrawColor(platform->renderer, 255, 200, 64, 255);
            if (SDL_RenderDrawRect(platform->renderer, &area)) return false;
        }
    }
    SDL_RenderPresent(platform->renderer);
    return true;
}

static bool set_save_type(struct CupidGBA* gba, const char* name) {
    if (!name) return true;
    static const struct { const char* name; enum CupidSaveType type; } types[] = {
        {"sram", SAVE_SRAM}, {"sram512", SAVE_SRAM512}, {"flash512", SAVE_FLASH512},
        {"flash1m", SAVE_FLASH1M}, {"eeprom", SAVE_EEPROM}, {"eeprom512", SAVE_EEPROM512}, {"none", SAVE_NONE}
    };
    for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
        if (strcmp(name, types[i].name)) continue;
        cupid_savedata_force_type(&gba->memory.savedata, types[i].type);
        return true;
    }
    fprintf(stderr, "Error: Unknown save type '%s'\n", name);
    return false;
}

static bool flush_saves(struct CupidGBA* const cores[], char paths[4][512], unsigned count) {
    bool ok = true;
    for (unsigned i = 0; i < count; ++i) {
        if (!cupid_savedata_save_file(&cores[i]->memory.savedata, paths[i])) {
            fprintf(stderr, "Error: Cannot save console %u to %s: %s\n", i + 1, paths[i], strerror(errno));
            ok = false;
        }
    }
    return ok;
}

int cupid_platform_run_local(const char* const paths[], unsigned count, const char* bios,
                             const char* saveType, int scale, unsigned frames) {
    if (!paths || count < 2 || count > 4) return 1;
    struct CupidGBA* cores[4] = {0};
    struct CupidPlatform platforms[4] = {0};
    struct CupidPlatformAudioMix mix;
    SDL_Texture* textures[4] = {0};
    struct CupidCable* cable = NULL;
    char saves[4][512];
    unsigned created = 0;
    int result = 1;
    bool initialized = false, started = false;
    /* Validate every path and save before SDL starts or any console runs. */
    for (unsigned i = 0; i < count; ++i) {
        if (!cupid_platform_local_save_path(paths[i], i, saves[i], sizeof(saves[i]))) {
            fprintf(stderr, "Error: Save path is too long: %s\n", paths[i]);
            goto cleanup;
        }
        cores[i] = calloc(1, sizeof(*cores[i]));
        if (!cores[i]) goto cleanup;
        cupid_gba_create(cores[i]);
        ++created;
        if (!cupid_load_rom(cores[i], paths[i]) || !set_save_type(cores[i], saveType)) goto cleanup;
        if (bios && !cupid_load_bios(cores[i], bios)) goto cleanup;
        cupid_gba_reset(cores[i]);
        if (!cupid_savedata_load_file(&cores[i]->memory.savedata, saves[i]) && errno != ENOENT) {
            fprintf(stderr, "Error: Cannot load save %s: %s\n", saves[i], strerror(errno));
            goto cleanup;
        }
    }
    initialized = true;
    if (!cupid_platform_init(&platforms[0], scale)) goto cleanup;
    SDL_SetWindowSize(platforms[0].window, GBA_SCREEN_WIDTH * 2 * scale,
                      GBA_SCREEN_HEIGHT * (count > 2 ? 2 : 1) * scale);
    textures[0] = platforms[0].texture;
    for (unsigned i = 1; i < count; ++i) {
        platforms[i].controllerIndex = -1;
        textures[i] = SDL_CreateTexture(platforms[0].renderer, SDL_PIXELFORMAT_ABGR8888,
                                        SDL_TEXTUREACCESS_STREAMING, GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT);
        if (!textures[i]) goto cleanup;
    }
    cable = cupid_cable_create(cores, count);
    if (!cable) { fprintf(stderr, "Error: Cannot create local cable session\n"); goto cleanup; }
    if (!cupid_platform_init_audio_mix(&platforms[0], &mix, cores, count))
        fprintf(stderr, "Warning: Audio initialization failed: %s\n", SDL_GetError());
    printf("CupidGBA started: %u local consoles; Tab selects keyboard, F9 connects or disconnects\n", count);
    started = true;
    result = 0;
    unsigned focus = 0, completed = 0;
    bool connected = true;
    const uint64_t frequency = SDL_GetPerformanceFrequency();
    const uint64_t frameTicks = (uint64_t)((double)frequency * VIDEO_TOTAL_LENGTH / GBA_CLOCK_FREQ + 0.5);
    uint64_t next = SDL_GetPerformanceCounter() + frameTicks;
    while (!frames || completed < frames) {
        if (!cupid_platform_poll_local(platforms, cores, count, &focus, &connected)) break;
        /* One device consumes all queues. Hold its lock while cores produce. */
        if (platforms[0].audioDevice) SDL_LockAudioDevice(platforms[0].audioDevice);
        cupid_cable_connect(cable, connected);
        bool ran = cupid_cable_run(cable, VIDEO_TOTAL_LENGTH);
        if (platforms[0].audioDevice) SDL_UnlockAudioDevice(platforms[0].audioDevice);
        if (!ran || !cupid_platform_render_local(&platforms[0], textures, cores, count, focus)) {
            fprintf(stderr, "Error: Local session stopped: %s\n", SDL_GetError());
            result = 1; break;
        }
        ++completed;
        char title[128];
        snprintf(title, sizeof(title), "CupidGBA - Console %u keyboard - %s", focus + 1, connected ? "Connected" : "Disconnected");
        SDL_SetWindowTitle(platforms[0].window, title);
        if (!(completed % 300) && !flush_saves(cores, saves, count)) result = 1;
        uint64_t now = SDL_GetPerformanceCounter();
        if (now < next) {
            uint64_t delay = (next - now) * 1000 / frequency;
            if (delay > 1) SDL_Delay((uint32_t)delay - 1);
            while (SDL_GetPerformanceCounter() < next) {}
        } else if (now - next > frameTicks * 4) next = now;
        next += frameTicks;
    }
    printf("Local session finished: %u frames\n", completed);
cleanup:
    if (started && !flush_saves(cores, saves, count)) result = 1;
    /* Stop the callback before releasing the queues it borrows. */
    if (platforms[0].audioDevice) {
        SDL_CloseAudioDevice(platforms[0].audioDevice);
        platforms[0].audioDevice = 0;
    }
    cupid_cable_destroy(cable);
    for (unsigned i = 1; i < count; ++i) {
        if (platforms[i].controller) SDL_GameControllerClose(platforms[i].controller);
        if (textures[i]) SDL_DestroyTexture(textures[i]);
    }
    if (initialized) cupid_platform_deinit(&platforms[0]);
    for (unsigned i = 0; i < created; ++i) {
        cupid_gba_destroy(cores[i]); free(cores[i]);
    }
    return result;
}
