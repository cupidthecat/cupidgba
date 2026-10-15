/**
 * @file input.c
 * @brief Keyboard and controller to GBA key mapping.
 *
 * GBA KEYINPUT register (0x04000130) is active-low:
 * bit 0 = A, 1 = B, 2 = Select, 3 = Start,
 * bit 4 = Right, 5 = Left, 6 = Up, 7 = Down,
 * bit 8 = R, 9 = L.
 */

#include "cupid/platform.h"
#include <stdio.h>

/**
 * @brief GBA key bit indices.
 */
enum {
    GBA_KEY_A      = 0,
    GBA_KEY_B      = 1,
    GBA_KEY_SELECT = 2,
    GBA_KEY_START  = 3,
    GBA_KEY_RIGHT  = 4,
    GBA_KEY_LEFT   = 5,
    GBA_KEY_UP     = 6,
    GBA_KEY_DOWN   = 7,
    GBA_KEY_R      = 8,
    GBA_KEY_L      = 9
};

/**
 * @brief Keyboard to GBA mapping.
 */
static const struct {
    SDL_Scancode scancode;
    int bit;
} keyMap[] = {
    { SDL_SCANCODE_Z,         GBA_KEY_A      },
    { SDL_SCANCODE_X,         GBA_KEY_B      },
    { SDL_SCANCODE_BACKSPACE, GBA_KEY_SELECT },
    { SDL_SCANCODE_RETURN,    GBA_KEY_START  },
    { SDL_SCANCODE_RIGHT,     GBA_KEY_RIGHT  },
    { SDL_SCANCODE_LEFT,      GBA_KEY_LEFT   },
    { SDL_SCANCODE_UP,        GBA_KEY_UP     },
    { SDL_SCANCODE_DOWN,      GBA_KEY_DOWN   },
    { SDL_SCANCODE_S,         GBA_KEY_R      },
    { SDL_SCANCODE_A,         GBA_KEY_L      },
};
#define KEY_MAP_SIZE (sizeof(keyMap) / sizeof(keyMap[0]))

/**
 * @brief Controller button to GBA mapping.
 */
static const struct {
    SDL_GameControllerButton button;
    int bit;
} ctrlBtnMap[] = {
    { SDL_CONTROLLER_BUTTON_A,             GBA_KEY_A      },
    { SDL_CONTROLLER_BUTTON_B,             GBA_KEY_B      },
    { SDL_CONTROLLER_BUTTON_BACK,          GBA_KEY_SELECT },
    { SDL_CONTROLLER_BUTTON_START,         GBA_KEY_START  },
    { SDL_CONTROLLER_BUTTON_DPAD_RIGHT,    GBA_KEY_RIGHT  },
    { SDL_CONTROLLER_BUTTON_DPAD_LEFT,     GBA_KEY_LEFT   },
    { SDL_CONTROLLER_BUTTON_DPAD_UP,       GBA_KEY_UP     },
    { SDL_CONTROLLER_BUTTON_DPAD_DOWN,     GBA_KEY_DOWN   },
    { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, GBA_KEY_R      },
    { SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  GBA_KEY_L      },
};
#define CTRL_BTN_MAP_SIZE (sizeof(ctrlBtnMap) / sizeof(ctrlBtnMap[0]))

/**
 * @brief Selected controller buttons and stick directions.
 *
 * Left-stick X and Y use a +/-0x4000 dead-zone.
 */
static uint16_t controller_keys(const struct CupidPlatform* plat) {
    if (!plat->controller) return 0;

    int16_t lx = SDL_GameControllerGetAxis(plat->controller, SDL_CONTROLLER_AXIS_LEFTX);
    int16_t ly = SDL_GameControllerGetAxis(plat->controller, SDL_CONTROLLER_AXIS_LEFTY);

    uint16_t pressed = 0;
    for (size_t i = 0; i < CTRL_BTN_MAP_SIZE; i++) {
        if (SDL_GameControllerGetButton(plat->controller, ctrlBtnMap[i].button))
            pressed |= (uint16_t)(1u << ctrlBtnMap[i].bit);
    }
    if (lx >  CUPID_AXIS_DEADZONE) pressed |= (1 << GBA_KEY_RIGHT);
    if (lx < -CUPID_AXIS_DEADZONE) pressed |= (1 << GBA_KEY_LEFT);
    if (ly >  CUPID_AXIS_DEADZONE) pressed |= (1 << GBA_KEY_DOWN);
    if (ly < -CUPID_AXIS_DEADZONE) pressed |= (1 << GBA_KEY_UP);
    return pressed;
}

static void open_controller(struct CupidPlatform* plat, int index) {
    if (!SDL_IsGameController(index)) return;
    plat->controller = SDL_GameControllerOpen(index);
    if (!plat->controller) return;
    plat->controllerIndex = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(plat->controller));
    cupid_platform_rumble_refresh(plat);
    printf("CUPID: Controller connected: %s\n", SDL_GameControllerName(plat->controller));
}

/**
 * @brief Poll SDL events and update key state.
 */

bool cupid_platform_poll_events(struct CupidPlatform* plat, struct CupidGBA* gba) {
    SDL_JoystickID removed = -1;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            plat->running = false;
            return false;

        /** Keyboard events. */
        case SDL_KEYDOWN:
            if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                plat->running = false;
                return false;
            }
            if (gba->cartridge.tiltPresent) {
                int x = gba->cartridge.tilt.inputX, y = gba->cartridge.tilt.inputY;
                switch (event.key.keysym.scancode) {
                case SDL_SCANCODE_F1: x -= 32; break;
                case SDL_SCANCODE_F2: x += 32; break;
                case SDL_SCANCODE_F3: y -= 32; break;
                case SDL_SCANCODE_F4: y += 32; break;
                case SDL_SCANCODE_F10: x = y = CUPID_TILT_NEUTRAL; break;
                default: break;
                }
                cupid_cartridge_set_tilt_sample(gba,
                    (uint16_t)(x < 0 ? 0 : x > 4095 ? 4095 : x),
                    (uint16_t)(y < 0 ? 0 : y > 4095 ? 4095 : y));
            }
            if (gba->cartridge.solarPresent &&
                (event.key.keysym.scancode == SDL_SCANCODE_F5 || event.key.keysym.scancode == SDL_SCANCODE_F6)) {
                int level = 255 - gba->cartridge.solar.threshold;
                level += event.key.keysym.scancode == SDL_SCANCODE_F5 ? -16 : 16;
                cupid_cartridge_set_solar_level(gba, (uint8_t)(level < 0 ? 0 : level > 255 ? 255 : level));
            }
            if (gba->cartridge.gyroPresent &&
                (event.key.keysym.scancode == SDL_SCANCODE_F7 ||
                 event.key.keysym.scancode == SDL_SCANCODE_F8 ||
                 event.key.keysym.scancode == SDL_SCANCODE_F9)) {
                int sample = gba->cartridge.gyro.sample;
                if (event.key.keysym.scancode == SDL_SCANCODE_F9) sample = CUPID_GYRO_NEUTRAL;
                else sample += event.key.keysym.scancode == SDL_SCANCODE_F7 ? -128 : 128;
                cupid_cartridge_set_gyro_sample(gba,
                    (uint16_t)(sample < 0 ? 0 : sample > 4095 ? 4095 : sample));
            }
            for (size_t i = 0; i < KEY_MAP_SIZE; i++) {
                if (event.key.keysym.scancode == keyMap[i].scancode) {
                    plat->keyboardKeys |= (uint16_t)(1u << keyMap[i].bit);
                }
            }
            break;

        case SDL_KEYUP:
            for (size_t i = 0; i < KEY_MAP_SIZE; i++) {
                if (event.key.keysym.scancode == keyMap[i].scancode) {
                    plat->keyboardKeys &= (uint16_t)~(1u << keyMap[i].bit);
                }
            }
            break;

        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST &&
                (!plat->window || event.window.windowID == SDL_GetWindowID(plat->window)))
                plat->keyboardKeys = 0;
            break;

        /** Hot-plug events. */
        case SDL_CONTROLLERDEVICEADDED:
            if (!plat->controller) open_controller(plat, event.cdevice.which);
            break;

        case SDL_CONTROLLERDEVICEREMOVED:
            if (plat->controller &&
                event.cdevice.which == plat->controllerIndex) {
                printf("CUPID: Controller disconnected\n");
                removed = event.cdevice.which;
                cupid_platform_rumble_stop(plat);
                SDL_GameControllerClose(plat->controller);
                plat->controller = NULL;
                plat->controllerIndex = -1;
            }
            break;
        }
    }

    if (!plat->controller) {
        for (int index = 0; index < SDL_NumJoysticks() && !plat->controller; index++) {
            if (SDL_JoystickGetDeviceInstanceID(index) != removed) open_controller(plat, index);
        }
    }
    /* Controller state is sampled from its selected handle. Keyboard, button
     * and stick holds combine without releasing another source's key. */
    gba->keysActive = plat->keyboardKeys | controller_keys(plat);

    /** Test keypad IRQ after key state changes. */
    cupid_gba_test_keypad_irq(gba);

    return true;
}
