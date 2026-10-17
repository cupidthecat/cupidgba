/**
 * @file main.c
 * @brief SDL2 entry point, CLI parsing, initialization, and main loop.
 */

#include "cupid/gba.h"
#include "cupid/platform.h"
#include "cupid/platform/rtc.h"
#include "cupid/platform/peripherals.h"
#include "local.h"
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Frames between automatic save flushes.
 */
#define SAVE_FLUSH_FRAMES 300

static bool flush_rtc(struct CupidGBA* gba, struct CupidPlatformRTC* config) {
    uint64_t stamp = 0;
    if (gba->cartridge.rtcPresent && config->offlineWall && !cupid_platform_rtc_host_clock(NULL, &stamp)) return false;
    return cupid_platform_rtc_save(gba, config, stamp);
}

static void capture_completed_frame(void* context, const uint32_t* pixels, uint64_t clock) {
    (void)clock;
    memcpy(context, pixels, GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT * sizeof(*pixels));
}

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
 * @brief Print CLI usage.
 */

static void printUsage(const char* progname) {
    fprintf(stderr, "Usage: %s rom.gba [-b bios.bin] [-s scale] [-S savetype]\n", progname);
    fprintf(stderr, "       %s rom.gba --link peer.gba [--link third.gba] [--link fourth.gba] [--frames count]\n", progname);
    fprintf(stderr, "\nSave types (GBA): sram, sram512, flash512, flash1m, eeprom, eeprom512, none\n");
    fprintf(stderr, "\nRTC: --rtc auto|on|off  --rtc-time YYYY-MM-DDTHH:MM:SS\n");
    fprintf(stderr, "     --rtc-offline pause|wall (default: pause; wall includes powered-off time)\n");
    fprintf(stderr, "\nPeripherals: --solar auto|on|off  --sunlight 0..255 (default: 128)\n");
    fprintf(stderr, "             --rumble auto|on|off\n");
    fprintf(stderr, "             --gyro auto|on|off  --gyro-sample 0..4095 (default: 1728)\n");
    fprintf(stderr, "             --tilt auto|on|off  --tilt-x 0..4095  --tilt-y 0..4095 (defaults: 928)\n");
    fprintf(stderr, "\nControls:\n");
    fprintf(stderr, "  Z=A  X=B  Enter=Start  Backspace=Select\n");
    fprintf(stderr, "  Arrow keys=D-Pad  A=L  S=R  Esc=Quit\n");
    fprintf(stderr, "  F5=Less sunlight  F6=More sunlight\n");
    fprintf(stderr, "  F7=Lower gyro input  F8=Higher gyro input  F9=Nominal neutral\n");
    fprintf(stderr, "  F1/F2=Lower/higher tilt X  F3/F4=Lower/higher tilt Y  F10=Nominal neutral\n");
}

/**
 * @brief Program entry point.
 */

int main(int argc, char* argv[]) {
    const char* romPath = NULL;
    const char* biosPath = NULL;
    const char* saveTypeStr = NULL;
    int scale = CUPID_DEFAULT_SCALE;
    int rtcPresence = -1;
    struct CupidRTCDateTime rtcSeed;
    struct CupidPlatformRTC rtcConfig = {0};
    struct CupidPlatformPeripherals peripherals = CUPID_PLATFORM_PERIPHERALS_DEFAULT;
    const char* linked[4] = {0};
    unsigned peerCount = 0, frames = 0;

    /** Parse CLI arguments. */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--link") == 0 && i + 1 < argc && peerCount < 3) {
            linked[++peerCount] = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            char* end;
            errno = 0;
            const char* text = argv[++i];
            unsigned long value = strtoul(text, &end, 10);
            if (text[0] < '0' || text[0] > '9' || *end || errno || !value || value > UINT_MAX) {
                fprintf(stderr, "Error: Invalid frame count: %s\n", text); return 1;
            }
            frames = (unsigned)value;
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            biosPath = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            scale = atoi(argv[++i]);
            if (scale < 1) scale = 1;
            if (scale > 10) scale = 10;
        } else if (strcmp(argv[i], "-S") == 0 && i + 1 < argc) {
            saveTypeStr = argv[++i];
        } else if ((!strcmp(argv[i], "--solar") || !strcmp(argv[i], "--rumble") ||
                    !strcmp(argv[i], "--gyro") || !strcmp(argv[i], "--tilt")) && i + 1 < argc) {
            int* presence = !strcmp(argv[i], "--solar") ? &peripherals.solarPresence :
                !strcmp(argv[i], "--rumble") ? &peripherals.rumblePresence :
                !strcmp(argv[i], "--gyro") ? &peripherals.gyroPresence : &peripherals.tiltPresence;
            if (!cupid_platform_peripheral_mode(argv[++i], presence)) {
                fprintf(stderr, "Error: Peripheral mode must be auto, on or off\n"); return 1;
            }
        } else if (!strcmp(argv[i], "--sunlight") && i + 1 < argc) {
            if (!cupid_platform_solar_level(argv[++i], &peripherals.solarLevel)) {
                fprintf(stderr, "Error: Sunlight must be an integer from 0 through 255\n"); return 1;
            }
        } else if (!strcmp(argv[i], "--gyro-sample") && i + 1 < argc) {
            if (!cupid_platform_gyro_sample(argv[++i], &peripherals.gyroSample)) {
                fprintf(stderr, "Error: Gyro input must be an integer from 0 through 4095\n"); return 1;
            }
        } else if ((!strcmp(argv[i], "--tilt-x") || !strcmp(argv[i], "--tilt-y")) && i + 1 < argc) {
            uint16_t* axis = !strcmp(argv[i], "--tilt-x") ? &peripherals.tiltX : &peripherals.tiltY;
            if (!cupid_platform_sample12(argv[++i], axis)) {
                fprintf(stderr, "Error: Tilt input must be an integer from 0 through 4095\n"); return 1;
            }
        } else if (strcmp(argv[i], "--rtc") == 0 && i + 1 < argc) {
            const char* mode = argv[++i];
            if (!strcmp(mode, "auto")) rtcPresence = -1;
            else if (!strcmp(mode, "on")) rtcPresence = 1;
            else if (!strcmp(mode, "off")) rtcPresence = 0;
            else { fprintf(stderr, "Error: Invalid RTC mode: %s\n", mode); return 1; }
        } else if (strcmp(argv[i], "--rtc-time") == 0 && i + 1 < argc) {
            if (!cupid_rtc_parse_datetime(argv[++i], &rtcSeed)) {
                fprintf(stderr, "Error: Invalid RTC time; use a valid date from 2000 through 2099\n"); return 1;
            }
            rtcConfig.seed = &rtcSeed;
        } else if (strcmp(argv[i], "--rtc-offline") == 0 && i + 1 < argc) {
            const char* mode = argv[++i];
            if (!strcmp(mode, "pause")) rtcConfig.offlineWall = false;
            else if (!strcmp(mode, "wall")) rtcConfig.offlineWall = true;
            else { fprintf(stderr, "Error: Invalid RTC offline mode: %s\n", mode); return 1; }
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

    if (peerCount) {
        linked[0] = romPath;
        const struct CupidLocalDevices devices = {rtcPresence, rtcConfig, peripherals};
        return cupid_platform_run_local(linked, peerCount + 1, biosPath, saveTypeStr, scale, frames, &devices);
    }
    if (frames) { fprintf(stderr, "Error: --frames requires --link\n"); return 1; }

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
    if (rtcPresence >= 0) cupid_cartridge_enable_rtc(&gba, rtcPresence != 0);
    cupid_platform_peripherals_configure(&gba, &peripherals);

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
            fprintf(stderr, "Error: Failed to load BIOS: %s\n", biosPath);
            cupid_gba_destroy(&gba);
            return 1;
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

    char rtcPath[sizeof(savePath)];
    memcpy(rtcPath, savePath, strlen(savePath) + 1);
    memcpy(rtcPath + strlen(rtcPath) - 4, ".rtc", 5);
    rtcConfig.path = rtcPath;
    struct CupidRTCDateTime hostDate;
    uint64_t hostStamp = 0;
    if (gba.cartridge.rtcPresent && rtcConfig.offlineWall && !cupid_platform_rtc_host_clock(&hostDate, &hostStamp)) {
        fprintf(stderr, "Error: Cannot read RTC host clock: %s\n", strerror(errno));
        cupid_gba_destroy(&gba); return 1;
    }
    if (!cupid_platform_rtc_load(&gba, &rtcConfig, rtcConfig.offlineWall ? &hostDate : NULL, hostStamp)) {
        fprintf(stderr, "Error: Cannot load RTC state %s: %s\n", rtcPath, strerror(errno));
        cupid_gba_destroy(&gba); return 1;
    }

    /** Initialize SDL platform. */
    struct CupidPlatform plat;
    if (!cupid_platform_init(&plat, scale)) {
        cupid_gba_destroy(&gba);
        return 1;
    }
    uint32_t* completedFrame = calloc(GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT, sizeof(*completedFrame));
    if (!completedFrame) {
        fprintf(stderr, "Error: Cannot allocate completed frame\n");
        cupid_platform_deinit(&plat);
        cupid_gba_destroy(&gba);
        return 1;
    }
    cupid_video_set_frame_callback(&gba.video, capture_completed_frame, completedFrame);
    cupid_cartridge_set_rumble_output(&gba, cupid_platform_rumble_changed, &plat);

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
     * 2. Run one frame-length interval, including the battery clock in STOP.
     * 3. Present framebuffer.
     * 4. Periodically flush dirty save data.
     */
    int saveFlushCounter = 0;
    while (plat.running) {
        /** Poll input. */
        if (!cupid_platform_poll_events(&plat, &gba)) break;
        cupid_platform_rumble_refresh(&plat);

        /* The SDL callback consumes the same queue that emulation produces. */
        if (plat.audioDevice) SDL_LockAudioDevice(plat.audioDevice);
        cupid_gba_run_interval(&gba, VIDEO_TOTAL_LENGTH);
        if (plat.audioDevice) SDL_UnlockAudioDevice(plat.audioDevice);

        /** Retain the last complete image while the next frame draws or STOP waits. */
        cupid_platform_render_frame(&plat, completedFrame);

        /** Periodic save flush. */
        if (++saveFlushCounter >= SAVE_FLUSH_FRAMES) {
            saveFlushCounter = 0;
            if (!cupid_savedata_save_file(&gba.memory.savedata, savePath))
                fprintf(stderr, "Error: Cannot save %s: %s\n", savePath, strerror(errno));
            if (!flush_rtc(&gba, &rtcConfig))
                fprintf(stderr, "Error: Cannot save RTC state %s: %s\n", rtcPath, strerror(errno));
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
    if (!flush_rtc(&gba, &rtcConfig)) {
        fprintf(stderr, "Error: Cannot save RTC state %s: %s\n", rtcPath, strerror(errno));
        result = 1;
    }
    cupid_cartridge_set_rumble_output(&gba, NULL, NULL);
    cupid_platform_deinit(&plat);
    cupid_video_set_frame_callback(&gba.video, NULL, NULL);
    free(completedFrame);
    cupid_gba_destroy(&gba);
    printf("CupidGBA exited.\n");
    return result;
}
