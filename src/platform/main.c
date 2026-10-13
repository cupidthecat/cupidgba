/**
 * @file main.c
 * @brief SDL2 entry point, CLI parsing, initialization, and main loop.
 */

#include "cupid/gba.h"
#include "cupid/platform.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Frames between automatic save flushes.
 */
#define SAVE_FLUSH_FRAMES 300

/**
 * @brief Derive a .sav path from a ROM path.
 */
static bool derive_save_path(const char* romPath, char* out, size_t outSize) {
    const char* lastSlash = strrchr(romPath, '/');
#ifdef _WIN32
    const char* backslash = strrchr(romPath, '\\');
    if (backslash && (!lastSlash || backslash > lastSlash)) lastSlash = backslash;
#endif
    const char* ext = strrchr(romPath, '.');
    size_t prefix = ext && (!lastSlash || ext > lastSlash)
        ? (size_t)(ext - romPath) : strlen(romPath);
    if (outSize < 5 || prefix > outSize - 5) return false;
    memcpy(out, romPath, prefix);
    memcpy(out + prefix, ".sav", 5);
    return true;
}

/**
 * @brief Platform implementation.
 */

bool cupid_platform_init(struct CupidPlatform* plat, int scale) {
    memset(plat, 0, sizeof(*plat));
    plat->scale = scale;
    plat->windowWidth = GBA_SCREEN_WIDTH * scale;
    plat->windowHeight = GBA_SCREEN_HEIGHT * scale;
    plat->running = true;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    plat->window = SDL_CreateWindow(
        "CupidGBA",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        plat->windowWidth, plat->windowHeight,
        SDL_WINDOW_SHOWN
    );
    if (!plat->window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    plat->renderer = SDL_CreateRenderer(plat->window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!plat->renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }

    plat->texture = SDL_CreateTexture(plat->renderer,
        SDL_PIXELFORMAT_ABGR8888,
        SDL_TEXTUREACCESS_STREAMING,
        GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT);
    if (!plat->texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }

    /** Open the first available game controller if present. */
    plat->controller = NULL;
    plat->controllerIndex = -1;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            plat->controller = SDL_GameControllerOpen(i);
            if (plat->controller) {
                plat->controllerIndex = i;
                printf("CUPID: Opened controller: %s\n",
                       SDL_GameControllerName(plat->controller));
                break;
            }
        }
    }

    return true;
}

void cupid_platform_deinit(struct CupidPlatform* plat) {
    if (plat->audioDevice) SDL_CloseAudioDevice(plat->audioDevice);
    if (plat->controller) SDL_GameControllerClose(plat->controller);
    if (plat->texture) SDL_DestroyTexture(plat->texture);
    if (plat->renderer) SDL_DestroyRenderer(plat->renderer);
    if (plat->window) SDL_DestroyWindow(plat->window);
    SDL_Quit();
}

void cupid_platform_render_frame(struct CupidPlatform* plat, const uint32_t* framebuffer) {
    void* pixels;
    int pitch;
    if (SDL_LockTexture(plat->texture, NULL, &pixels, &pitch) == 0) {
        /**
         * Copy each row while respecting texture pitch.
         * Pitch may differ from GBA_SCREEN_WIDTH * 4.
         */
        const uint8_t* src = (const uint8_t*)framebuffer;
        uint8_t* dst = (uint8_t*)pixels;
        int rowBytes = GBA_SCREEN_WIDTH * (int)sizeof(uint32_t);
        for (int y = 0; y < GBA_SCREEN_HEIGHT; y++) {
            memcpy(dst, src, rowBytes);
            src += rowBytes;
            dst += pitch;
        }
        SDL_UnlockTexture(plat->texture);
    }
    SDL_RenderClear(plat->renderer);
    SDL_RenderCopy(plat->renderer, plat->texture, NULL, NULL);
    SDL_RenderPresent(plat->renderer);  /** VSync wait. */
}

/**
 * @brief Print CLI usage.
 */

static void printUsage(const char* progname) {
    fprintf(stderr, "Usage: %s rom.gba [-b bios.bin] [-s scale] [-S savetype]\n", progname);
    fprintf(stderr, "\nSave types (GBA): sram, sram512, flash512, flash1m, eeprom, eeprom512, none\n");
    fprintf(stderr, "\nControls:\n");
    fprintf(stderr, "  Z=A  X=B  Enter=Start  Backspace=Select\n");
    fprintf(stderr, "  Arrow keys=D-Pad  A=L  S=R  Esc=Quit\n");
}

/**
 * @brief Program entry point.
 */

int main(int argc, char* argv[]) {
    const char* romPath = NULL;
    const char* biosPath = NULL;
    const char* saveTypeStr = NULL;
    int scale = CUPID_DEFAULT_SCALE;

    /** Parse CLI arguments. */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            biosPath = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            scale = atoi(argv[++i]);
            if (scale < 1) scale = 1;
            if (scale > 10) scale = 10;
        } else if (strcmp(argv[i], "-S") == 0 && i + 1 < argc) {
            saveTypeStr = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            romPath = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            printUsage(argv[0]);
            return 1;
        }
    }

    if (!romPath) {
        fprintf(stderr, "Error: No ROM file specified.\n");
        printUsage(argv[0]);
        return 1;
    }

    /** GBA path. */

    /** Derive save-file path (.sav alongside the ROM). */
    char savePath[512];
    if (!derive_save_path(romPath, savePath, sizeof(savePath))) {
        fprintf(stderr, "Error: Save path is too long: %s\n", romPath);
        return 1;
    }

    /** Create GBA instance. */
    struct CupidGBA gba;
    cupid_gba_create(&gba);

    /** Load ROM. */
    if (!cupid_load_rom(&gba, romPath)) {
        fprintf(stderr, "Error: Failed to load ROM: %s\n", romPath);
        cupid_gba_destroy(&gba);
        return 1;
    }
    printf("Loaded ROM: %s (%zu bytes)\n", romPath, gba.memory.romSize);

    /** Override save type if specified on CLI. */
    if (saveTypeStr) {
        enum CupidSaveType stype = SAVE_AUTODETECT;
        if (strcmp(saveTypeStr, "sram") == 0)          stype = SAVE_SRAM;
        else if (strcmp(saveTypeStr, "sram512") == 0)   stype = SAVE_SRAM512;
        else if (strcmp(saveTypeStr, "flash512") == 0)  stype = SAVE_FLASH512;
        else if (strcmp(saveTypeStr, "flash1m") == 0)   stype = SAVE_FLASH1M;
        else if (strcmp(saveTypeStr, "eeprom") == 0)    stype = SAVE_EEPROM;
        else if (strcmp(saveTypeStr, "eeprom512") == 0) stype = SAVE_EEPROM512;
        else if (strcmp(saveTypeStr, "none") == 0)      stype = SAVE_NONE;
        else {
            fprintf(stderr, "Warning: Unknown save type '%s', using auto-detect\n", saveTypeStr);
        }
        if (stype != SAVE_AUTODETECT) {
            cupid_savedata_force_type(&gba.memory.savedata, stype);
            printf("Save type forced: %s\n", saveTypeStr);
        }
    }

    /** Load BIOS (optional). */
    if (biosPath) {
        if (!cupid_load_bios(&gba, biosPath)) {
            fprintf(stderr, "Warning: Failed to load BIOS: %s (using HLE)\n", biosPath);
        } else {
            gba.biosHLE = false;
            printf("Loaded BIOS: %s\n", biosPath);
        }
    }

    /** Reset system. */
    cupid_gba_reset(&gba);

    /** Load persisted save data (no-op when .sav does not exist). */
    if (!cupid_savedata_load_file(&gba.memory.savedata, savePath) && errno != ENOENT) {
        const char* reason = errno == EINVAL
            ? "unsupported save size or incompatible save type; check the -S option"
            : strerror(errno);
        fprintf(stderr, "Error: Cannot load save %s: %s\n", savePath, reason);
        cupid_gba_destroy(&gba);
        return 1;
    }

    /** Initialize SDL platform. */
    struct CupidPlatform plat;
    if (!cupid_platform_init(&plat, scale)) {
        cupid_gba_destroy(&gba);
        return 1;
    }

    /** Initialize audio. */
    if (!cupid_platform_init_audio(&plat, &gba)) {
        fprintf(stderr, "Warning: Audio initialization failed, continuing without audio\n");
    }

    printf("CupidGBA started — %dx%d (scale %d)\n",
           plat.windowWidth, plat.windowHeight, scale);

    /**
     * Frame pacing target: real GBA frame period.
     *
     * Keeps emulation at ~59.73 Hz (VIDEO_TOTAL_LENGTH / GBA_CLOCK_FREQ)
     * even when the display refresh is 60 Hz, preventing audio lag drift.
     */
    const uint64_t perfFreq = SDL_GetPerformanceFrequency();
    const uint64_t frameTicks = (uint64_t)(((double)perfFreq *
        (double)VIDEO_TOTAL_LENGTH / (double)GBA_CLOCK_FREQ) + 0.5);
    uint64_t nextFrameTick = SDL_GetPerformanceCounter() + frameTicks;

    /**
     * Main loop (single-threaded):
     * 1. Poll SDL events and map keys.
     * 2. Run one full emulation frame.
     * 3. Present framebuffer.
     * 4. Periodically flush dirty save data.
     */
    int saveFlushCounter = 0;
    while (plat.running) {
        /** Poll input. */
        if (!cupid_platform_poll_events(&plat, &gba)) break;

        /* The SDL callback consumes the same queue that emulation produces. */
        if (plat.audioDevice) SDL_LockAudioDevice(plat.audioDevice);
        cupid_gba_run_frame(&gba);
        if (plat.audioDevice) SDL_UnlockAudioDevice(plat.audioDevice);

        /** Present framebuffer. */
        cupid_platform_render_frame(&plat, gba.video.outputBuffer);

        /** Periodic save flush. */
        if (++saveFlushCounter >= SAVE_FLUSH_FRAMES) {
            saveFlushCounter = 0;
            if (!cupid_savedata_save_file(&gba.memory.savedata, savePath))
                fprintf(stderr, "Error: Cannot save %s: %s\n", savePath, strerror(errno));
        }

        /**
         * Pace to hardware frame length.
         *
         * Use coarse SDL_Delay for larger remaining times, then a short
         * spin-wait for sub-millisecond precision.
         */
        uint64_t now = SDL_GetPerformanceCounter();
        if (now < nextFrameTick) {
            uint64_t remaining = nextFrameTick - now;
            uint64_t coarseDelayTicks = perfFreq / 500;  /** ~2 ms */
            if (remaining > coarseDelayTicks) {
                uint32_t delayMs = (uint32_t)((remaining * 1000) / perfFreq);
                if (delayMs > 1) {
                    SDL_Delay(delayMs - 1);
                }
            }
            while ((now = SDL_GetPerformanceCounter()) < nextFrameTick) {
            }
        } else if (now - nextFrameTick > frameTicks * 4) {
            /** If far behind (pause/debugger), re-anchor to current time. */
            nextFrameTick = now;
        }
        nextFrameTick += frameTicks;
    }

    /** Cleanup and flush unsaved progress before exit. */
    int result = 0;
    if (!cupid_savedata_save_file(&gba.memory.savedata, savePath)) {
        fprintf(stderr, "Error: Cannot save %s: %s\n", savePath, strerror(errno));
        result = 1;
    }
    cupid_platform_deinit(&plat);
    cupid_gba_destroy(&gba);
    printf("CupidGBA exited.\n");
    return result;
}
