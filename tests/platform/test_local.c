/* SPDX-License-Identifier: GPL-3.0-or-later */
#define SDL_MAIN_HANDLED
#include "cupid/platform.h"
#include "cupid/cable.h"
#include "../../src/platform/local.h"
#include "../test.h"
#include <string.h>

static void key_event(SDL_Scancode code, bool pressed, bool repeat) {
    SDL_Event event;
    /* SDL_Event is a union. C11 {0} initializes its first member only; the
     * keyboard repeat flag needs a defined value on every compiler. */
    SDL_zero(event);
    event.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.repeat = repeat;
    event.key.keysym.scancode = code;
    CHECK(SDL_PushEvent(&event) == 1);
}

static void key(SDL_Scancode code, bool pressed) { key_event(code, pressed, false); }

static void input(void) {
    CHECK(SDL_Init(SDL_INIT_GAMECONTROLLER) == 0);
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    struct CupidPlatform platforms[4] = {0};
    struct CupidGBA* cores[4];
    for (unsigned i = 0; i < 4; ++i) {
        cores[i] = calloc(1, sizeof(*cores[i])); CHECK(cores[i]);
        cupid_gba_create(cores[i]); cupid_gba_reset(cores[i]);
    }
    SDL_VirtualJoystickDesc desc = {0};
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX; desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    desc.button_mask = (1u << SDL_CONTROLLER_BUTTON_MAX) - 1;
    desc.axis_mask = (1u << SDL_CONTROLLER_AXIS_MAX) - 1;
    desc.vendor_id = 0xFFFF; desc.product_id = 0xCAFE; desc.name = "CupidGBA local input";
    CHECK(SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT, "0xFFFF/0xCAFE"));
    CHECK(SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1"));
    SDL_Joystick* joysticks[4];
    for (unsigned i = 0; i < 4; ++i) {
        int device = SDL_JoystickAttachVirtualEx(&desc); CHECK(device >= 0);
        joysticks[i] = SDL_JoystickOpen(device); CHECK(joysticks[i]);
    }
    unsigned focus = 0; bool connected = true;
    CHECK(cupid_platform_poll_local(platforms, cores, 4, &focus, &connected));
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(platforms[i].controller);
        for (unsigned j = 0; j < i; ++j) CHECK(platforms[i].controllerIndex != platforms[j].controllerIndex);
    }
    for (unsigned owner = 0; owner < 4; ++owner) {
        key(SDL_SCANCODE_Z, true);
        CHECK(cupid_platform_poll_local(platforms, cores, 4, &focus, &connected));
        for (unsigned i = 0; i < 4; ++i) CHECK(cores[i]->keysActive == (i == owner ? 1 : 0));
        key(SDL_SCANCODE_TAB, true);
        CHECK(cupid_platform_poll_local(platforms, cores, 4, &focus, &connected));
        CHECK(focus == (owner + 1) % 4);
        for (unsigned i = 0; i < 4; ++i) CHECK(!cores[i]->keysActive);
        key_event(SDL_SCANCODE_TAB, true, true);
        key_event(SDL_SCANCODE_Z, true, true);
        CHECK(cupid_platform_poll_local(platforms, cores, 4, &focus, &connected));
        CHECK(focus == (owner + 1) % 4);
        for (unsigned i = 0; i < 4; ++i) CHECK(!cores[i]->keysActive);
        key(SDL_SCANCODE_TAB, false);
        key(SDL_SCANCODE_Z, false);
        CHECK(cupid_platform_poll_local(platforms, cores, 4, &focus, &connected));
    }
    for (unsigned i = 0; i < 4; ++i) {
        cupid_cartridge_enable_solar(cores[i], true);
        cupid_cartridge_enable_gyro(cores[i], true);
        cupid_cartridge_enable_tilt(cores[i], true);
        cupid_cartridge_set_solar_level(cores[i], 128);
        CHECK(cupid_cartridge_set_gyro_sample(cores[i], 1000));
        CHECK(cupid_cartridge_set_tilt_sample(cores[i], 1000, 2000));
    }
    key(SDL_SCANCODE_F5, true); key(SDL_SCANCODE_F7, true); key(SDL_SCANCODE_F2, true);
    CHECK(cupid_platform_poll_local(platforms, cores, 4, &focus, &connected));
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(cores[i]->cartridge.solar.threshold == (i ? 127 : 143));
        CHECK(cores[i]->cartridge.gyro.sample == (i ? 1000 : 872));
        CHECK(cores[i]->cartridge.tilt.inputX == (i ? 1000 : 1032));
        CHECK(cores[i]->cartridge.tilt.inputY == 2000);
    }
    key_event(SDL_SCANCODE_F7, true, true);
    CHECK(cupid_platform_poll_local(platforms, cores, 4, &focus, &connected));
    CHECK(cores[0]->cartridge.gyro.sample == 872);
    for (unsigned i = 0; i < 4; ++i)
        CHECK(SDL_JoystickSetVirtualButton(joysticks[i], SDL_CONTROLLER_BUTTON_B, i == 2) == 0);
    SDL_JoystickUpdate();
    CHECK(cupid_platform_poll_local(platforms, cores, 4, &focus, &connected));
    for (unsigned i = 0; i < 4; ++i) CHECK(cores[i]->keysActive == (i == 2 ? 2 : 0));
    key(SDL_SCANCODE_F9, true);
    CHECK(cupid_platform_poll_local(platforms, cores, 4, &focus, &connected)); CHECK(!connected);
    key_event(SDL_SCANCODE_F9, true, true);
    CHECK(cupid_platform_poll_local(platforms, cores, 4, &focus, &connected)); CHECK(!connected);
    key(SDL_SCANCODE_F9, false);
    CHECK(cupid_platform_poll_local(platforms, cores, 4, &focus, &connected)); CHECK(!connected);
    key(SDL_SCANCODE_F9, true);
    CHECK(cupid_platform_poll_local(platforms, cores, 4, &focus, &connected)); CHECK(connected);
    CHECK(cores[0]->cartridge.gyro.sample == 872);
    key(SDL_SCANCODE_F11, true); key(SDL_SCANCODE_F10, true);
    CHECK(cupid_platform_poll_local(platforms, cores, 4, &focus, &connected));
    CHECK(cores[0]->cartridge.gyro.sample == CUPID_GYRO_NEUTRAL);
    CHECK(cores[0]->cartridge.tilt.inputX == CUPID_TILT_NEUTRAL);
    CHECK(cores[0]->cartridge.tilt.inputY == CUPID_TILT_NEUTRAL);
    CHECK(cores[1]->cartridge.gyro.sample == 1000 && cores[1]->cartridge.tilt.inputX == 1000);
    for (unsigned i = 0; i < 4; ++i) {
        SDL_GameControllerClose(platforms[i].controller); SDL_JoystickClose(joysticks[i]);
        cupid_gba_destroy(cores[i]); free(cores[i]);
    }
    for (int device = SDL_NumJoysticks() - 1; device >= 0; --device)
        if (SDL_JoystickIsVirtual(device)) CHECK(SDL_JoystickDetachVirtual(device) == 0);
    SDL_Quit();
}

static void video(void) {
    CHECK(SDL_setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
    CHECK(SDL_setenv("SDL_RENDER_DRIVER", "software", 1) == 0);
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    struct CupidPlatform platform = {0};
    platform.window = SDL_CreateWindow("CupidGBA local video", 0, 0, 480, 320, 0); CHECK(platform.window);
    platform.renderer = SDL_CreateRenderer(platform.window, -1, SDL_RENDERER_SOFTWARE); CHECK(platform.renderer);
    struct CupidGBA* cores[4]; SDL_Texture* textures[4]; const uint32_t* frames[4];
    const uint32_t colors[] = {0xFF0000FF, 0xFF00FF00, 0xFFFF0000, 0xFF556677};
    for (unsigned i = 0; i < 4; ++i) {
        cores[i] = calloc(1, sizeof(*cores[i])); CHECK(cores[i]);
        cupid_gba_create(cores[i]);
        for (unsigned p = 0; p < GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT; ++p) cores[i]->video.outputBuffer[p] = colors[i];
        frames[i] = cores[i]->video.outputBuffer;
        textures[i] = SDL_CreateTexture(platform.renderer, SDL_PIXELFORMAT_ABGR8888,
                         SDL_TEXTUREACCESS_STREAMING, 240, 160); CHECK(textures[i]);
    }
    uint32_t* pixels = malloc(480 * 320 * sizeof(*pixels)); CHECK(pixels);
    for (unsigned count = 2; count <= 4; ++count) {
        CHECK(cupid_platform_render_local(&platform, textures, frames, count, count - 1));
        CHECK(SDL_RenderReadPixels(platform.renderer, NULL, SDL_PIXELFORMAT_ABGR8888, pixels, 480 * 4) == 0);
        for (unsigned i = 0; i < count; ++i) {
            unsigned x = (i % 2) * 240 + 120, y = count == 2 ? 160 : (i / 2) * 160 + 80;
            CHECK(pixels[y * 480 + x] == colors[i]);
        }
        if (count == 3) CHECK(pixels[240 * 480 + 360] == 0xFF000000);
    }
    free(pixels);
    for (unsigned i = 0; i < 4; ++i) {
        SDL_DestroyTexture(textures[i]); cupid_gba_destroy(cores[i]); free(cores[i]);
    }
    SDL_DestroyRenderer(platform.renderer); SDL_DestroyWindow(platform.window); SDL_Quit();
}

static void audio(void) {
    struct CupidPlatformAudioMix mix = {0};
    struct CupidAudio sources[4] = {0};
    for (unsigned count = 2; count <= 4; ++count) {
        memset(sources, 0, sizeof(sources)); memset(&mix, 0, sizeof(mix));
        mix.count = count;
        int expected = 0;
        for (unsigned i = 0; i < count; ++i) {
            struct CupidAudio* source = &sources[i];
            int16_t value = (int16_t)((int)i * 4096 - 12288); expected += value;
            for (unsigned sample = 0; sample < 100; ++sample) {
                source->sampleBuffer[sample * 2] = value;
                source->sampleBuffer[sample * 2 + 1] = (int16_t)-value;
                source->sampleClocks[sample] = 512;
            }
            source->sampleWritePos = 200;
            mix.sources[i].audio = source; mix.sources[i].hostRate = 48000;
        }
        int16_t output[512];
        cupid_platform_audio_mix_callback(&mix, (uint8_t*)output, sizeof(output));
        for (unsigned i = 0; i < 100; ++i) {
            CHECK(output[i * 2] == expected / (int)count);
            CHECK(output[i * 2 + 1] == -expected / (int)count);
        }
        for (unsigned i = 0; i < count; ++i) CHECK(sources[i].sampleReadPos > 0);
    }
}

struct Playback { int16_t expected; unsigned frames; };
static void playback(void* context, const int16_t* pcm, int frames, int valid) {
    (void)frames;
    struct Playback* playback = context;
    for (int i = 0; i < valid; ++i) {
        CHECK(pcm[i * 2] == playback->expected && pcm[i * 2 + 1] == playback->expected);
        ++playback->frames;
    }
}

static void live_audio(void) {
    CHECK(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    CHECK(SDL_Init(SDL_INIT_AUDIO) == 0);
    struct CupidGBA* cores[4];
    struct Playback observations[4] = {0};
    for (unsigned i = 0; i < 4; ++i) {
        cores[i] = calloc(1, sizeof(*cores[i])); CHECK(cores[i]);
        cupid_gba_create(cores[i]); cupid_gba_reset(cores[i]); cores[i]->cpu.halted = true;
        cupid_io_write(cores[i], REG_SOUNDCNT_X, 0x80);
        cupid_io_write(cores[i], REG_SOUNDCNT_H, 0x0304);
        const uint32_t level = (i + 1) * 0x10;
        cupid_memory_store32(&cores[i]->cpu, GBA_BASE_IO + REG_FIFO_A_LO, level * 0x01010101u, NULL);
        cupid_audio_timer_overflow(&cores[i]->audio, 0, 0);
        observations[i].expected = (int16_t)((i + 1) * 3072);
    }
    struct CupidCable* cable = cupid_cable_create(cores, 4); CHECK(cable);
    struct CupidPlatform platform = {0}; struct CupidPlatformAudioMix mix;
    CHECK(cupid_platform_init_audio_mix(&platform, &mix, cores, 4));
    SDL_LockAudioDevice(platform.audioDevice);
    for (unsigned i = 0; i < 4; ++i) {
        mix.sources[i].outputSink = playback;
        mix.sources[i].outputSinkContext = &observations[i];
    }
    SDL_UnlockAudioDevice(platform.audioDevice);
    for (unsigned frame = 0; frame < 24; ++frame) {
        SDL_LockAudioDevice(platform.audioDevice);
        CHECK(cupid_cable_run(cable, VIDEO_TOTAL_LENGTH));
        SDL_UnlockAudioDevice(platform.audioDevice);
        SDL_Delay(2);
    }
    SDL_PauseAudioDevice(platform.audioDevice, 1);
    for (unsigned i = 0; i < 4; ++i) CHECK(observations[i].frames > 0);
    SDL_CloseAudioDevice(platform.audioDevice);
    cupid_cable_destroy(cable);
    for (unsigned i = 0; i < 4; ++i) { cupid_gba_destroy(cores[i]); free(cores[i]); }
    SDL_Quit();
}

static void paths(void) {
    char out[64];
    CHECK(cupid_platform_local_save_path("a/with.dots/game.gba", 0, out, sizeof(out)));
    CHECK(!strcmp(out, "a/with.dots/game.p1.sav"));
    CHECK(cupid_platform_local_save_path("a/with.dots/game", 3, out, sizeof(out)));
    CHECK(!strcmp(out, "a/with.dots/game.p4.sav"));
    CHECK(!cupid_platform_local_save_path("long/game.gba", 0, out, 12));
    CHECK(!cupid_platform_local_save_path("game.gba", 4, out, sizeof(out)));
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(cupid_platform_local_save_path("game.gba", i, out, sizeof(out)));
        CHECK(out[6] == (char)('1' + i));
    }
}

struct Publication {
    struct CupidGBA* cores[2];
    struct CupidCable* cable;
    struct CupidLocalFrames frames;
    unsigned count[2];
    uint64_t last[2];
};

static void publish(void* context, unsigned player, const uint32_t* pixels, uint64_t clock) {
    struct Publication* publication = context;
    CHECK(player < 2 && publication->cores[player]->video.vcount == 160);
    CHECK(clock > publication->last[player] && clock == cupid_cable_time(publication->cable));
    publication->last[player] = clock;
    ++publication->count[player];
    cupid_platform_capture_local_frame(&publication->frames, player, pixels, clock);
}

static void publication(void) {
    struct Publication p = {0};
    const uint16_t colors[] = {0x001F, 0x03E0};
    for (unsigned i = 0; i < 2; ++i) {
        p.cores[i] = calloc(1, sizeof(*p.cores[i])); CHECK(p.cores[i]);
        p.frames.pixels[i] = calloc(240 * 160, sizeof(uint32_t)); CHECK(p.frames.pixels[i]);
        cupid_gba_create(p.cores[i]); cupid_gba_reset(p.cores[i]); p.cores[i]->cpu.halted = true;
        cupid_io_write(p.cores[i], REG_DISPCNT, 0x0403);
        for (unsigned pixel = 0; pixel < 240 * 160; ++pixel) p.cores[i]->video.vram[pixel] = colors[i];
    }
    p.cores[0]->video.frameCounter = UINT32_MAX;
    p.cable = cupid_cable_create(p.cores, 2); CHECK(p.cable);
    cupid_cable_set_frame_callback(p.cable, publish, &p);
    CHECK(cupid_cable_run(p.cable, VIDEO_TOTAL_LENGTH * 2));
    CHECK(p.count[0] == 2 && p.count[1] == 2 && p.cores[0]->video.frameCounter == 1);
    for (unsigned i = 0; i < 2; ++i) {
        for (unsigned pixel = 0; pixel < 240 * 160; ++pixel) {
            CHECK(p.frames.pixels[i][pixel] == color16_to_rgba(colors[i]));
            p.cores[i]->video.outputBuffer[pixel] = 0xFF765432;
        }
        /* A live scanline buffer is the negative control: its new contents
         * must not replace the image handed to the frontend at VBlank. */
        CHECK(memcmp(p.frames.pixels[i], p.cores[i]->video.outputBuffer, 240 * 160 * 4) != 0);
    }
    p.cores[1]->stopped = true;
    CHECK(cupid_cable_run(p.cable, VIDEO_TOTAL_LENGTH));
    CHECK(p.count[0] == 3 && p.count[1] == 2);
    for (unsigned pixel = 0; pixel < 240 * 160; ++pixel)
        CHECK(p.frames.pixels[1][pixel] == color16_to_rgba(colors[1]));
    p.cores[1]->stopped = false;
    CHECK(cupid_cable_run(p.cable, VIDEO_TOTAL_LENGTH));
    CHECK(p.count[0] == 4 && p.count[1] == 3);
    cupid_cable_destroy(p.cable);
    for (unsigned i = 0; i < 2; ++i) {
        cupid_gba_destroy(p.cores[i]); free(p.cores[i]); free(p.frames.pixels[i]);
    }
}

int main(int argc, char** argv) {
    SDL_SetMainReady(); CHECK(argc == 2);
    if (!strcmp(argv[1], "input")) RUN(input);
    else if (!strcmp(argv[1], "video")) RUN(video);
    else if (!strcmp(argv[1], "audio")) RUN(audio);
    else if (!strcmp(argv[1], "live-audio")) RUN(live_audio);
    else if (!strcmp(argv[1], "paths")) RUN(paths);
    else if (!strcmp(argv[1], "publication")) RUN(publication);
    else CHECK(false);
    return 0;
}
