/**
 * @file platform.h
 * @brief SDL2 platform layer: window, input, audio output.
 */

#ifndef CUPID_PLATFORM_H
#define CUPID_PLATFORM_H

#include "cupid/types.h"
#include "cupid/gba.h"
#include <SDL2/SDL.h>

/** Scale factor for the window */
#define CUPID_DEFAULT_SCALE 3

/** Analog stick dead-zone **/
#define CUPID_AXIS_DEADZONE 0x4000

/**
 * @name Audio Resampler State
 *
 * Tracks fractional position for linear interpolation
 * from GBA_AUDIO_SAMPLE_RATE (32768 Hz) → host rate (48000 Hz).
 */
struct CupidPlatformAudio {
    struct CupidAudio* audio;   /**< Pointer to emulator audio state */
    int    hostRate;            /**< Actual device rate (may differ from requested) */
    double resampleStep;        /**< srcRate / dstRate (≈0.6827 for 32768→48000) */
    double resamplePos;         /**< Fractional position in source ring buffer */
    int16_t lastSampleL;        /**< Last output sample (for underrun fade) */
    int16_t lastSampleR;
};

/** @name Platform State */
struct CupidPlatform {
    SDL_Window*   window;
    SDL_Renderer* renderer;
    SDL_Texture*  texture;
    SDL_AudioDeviceID audioDevice;

    SDL_GameController* controller;   /**< First attached controller, or NULL */
    int controllerIndex;              /**< SDL joystick index of `controller` */

    struct CupidPlatformAudio platformAudio;

    int windowWidth;
    int windowHeight;
    int scale;

    bool running;
};

/** @name API */
bool cupid_platform_init(struct CupidPlatform* plat, int scale);
void cupid_platform_deinit(struct CupidPlatform* plat);

/** Upload framebuffer to SDL texture and present */
void cupid_platform_render_frame(struct CupidPlatform* plat, const uint32_t* framebuffer);

/** Poll SDL events, update key state. Returns false if quit requested. */
bool cupid_platform_poll_events(struct CupidPlatform* plat, struct CupidGBA* gba);

/** Audio callback */
void cupid_platform_audio_callback(void* userdata, uint8_t* stream, int len);

/** Init SDL audio device with callback */
bool cupid_platform_init_audio(struct CupidPlatform* plat, struct CupidGBA* gba);

#endif /* CUPID_PLATFORM_H */
