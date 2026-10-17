/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/platform.h"
#include <stdio.h>
#include <string.h>

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
    if (!plat->renderer) plat->renderer = SDL_CreateRenderer(plat->window, -1, SDL_RENDERER_SOFTWARE);
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
                plat->controllerIndex = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(plat->controller));
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
    SDL_RenderPresent(plat->renderer);  /* The chosen renderer may wait for VSync. */
}
