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
 * Tracks elapsed CPU clocks for interpolation between captured stereo frames.
 */
struct CupidPlatformAudio {
    struct CupidAudio* audio;   /**< Pointer to emulator audio state */
    int    hostRate;            /**< Actual device rate (may differ from requested) */
    double resampleStep;        /**< CPU clocks per host frame */
    double resamplePos;         /**< Clocks elapsed since the source anchor frame */
    uint64_t sampleOverruns;    /**< Last observed producer overrun count */
    int16_t lastSampleL;        /**< Last output sample (for underrun fade) */
    int16_t lastSampleR;
    /* Optional playback observer. Called on the SDL worker with a borrowed
     * stereo buffer; validFrames excludes the synthesized underrun tail. */
    void (*outputSink)(void* context, const int16_t* pcm, int frames, int validFrames);
    void* outputSinkContext;
};

/** @name Platform State */
struct CupidPlatform {
    SDL_Window*   window;
    SDL_Renderer* renderer;
    SDL_Texture*  texture;
    SDL_AudioDeviceID audioDevice;

    SDL_GameController* controller;   /**< First attached controller, or NULL */
    int controllerIndex;              /**< SDL joystick instance ID of `controller` */
    uint16_t keyboardKeys;            /**< Held GBA keys from keyboard events */

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
