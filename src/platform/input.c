/**
 * @file input.c
 * @brief Keyboard and controller to GBA and GB key mapping.
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
 * @brief Analog stick to d-pad mapping.
 *
 * Left-stick X and Y use a +/-0x4000 dead-zone.
 */
static void cupid_input_update_axes(struct CupidPlatform* plat, struct CupidGBA* gba) {
    if (!plat->controller) return;

    int16_t lx = SDL_GameControllerGetAxis(plat->controller, SDL_CONTROLLER_AXIS_LEFTX);
    int16_t ly = SDL_GameControllerGetAxis(plat->controller, SDL_CONTROLLER_AXIS_LEFTY);

    uint16_t axisBits = 0;
    if (lx >  CUPID_AXIS_DEADZONE) axisBits |= (1 << GBA_KEY_RIGHT);
    if (lx < -CUPID_AXIS_DEADZONE) axisBits |= (1 << GBA_KEY_LEFT);
    if (ly >  CUPID_AXIS_DEADZONE) axisBits |= (1 << GBA_KEY_DOWN);
    if (ly < -CUPID_AXIS_DEADZONE) axisBits |= (1 << GBA_KEY_UP);

    /** Clear d-pad bits then set those past the dead-zone threshold. */
    uint16_t mask = (1 << GBA_KEY_RIGHT) | (1 << GBA_KEY_LEFT)
                  | (1 << GBA_KEY_UP)    | (1 << GBA_KEY_DOWN);
    gba->keysActive &= ~mask;      /** release all d-pad */
    gba->keysActive |= axisBits;   /** press those past dead-zone */
}

/**
 * @brief Poll SDL events and update key state.
 */

bool cupid_platform_poll_events(struct CupidPlatform* plat, struct CupidGBA* gba) {
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
            for (size_t i = 0; i < KEY_MAP_SIZE; i++) {
                if (event.key.keysym.scancode == keyMap[i].scancode) {
                    gba->keysActive |= (1 << keyMap[i].bit);    /** bit=1 pressed */
                }
            }
            break;

        case SDL_KEYUP:
            for (size_t i = 0; i < KEY_MAP_SIZE; i++) {
                if (event.key.keysym.scancode == keyMap[i].scancode) {
                    gba->keysActive &= ~(1 << keyMap[i].bit);   /** bit=0 released */
                }
            }
            break;

        /** Controller button events. */
        case SDL_CONTROLLERBUTTONDOWN:
            for (size_t i = 0; i < CTRL_BTN_MAP_SIZE; i++) {
                if (event.cbutton.button == ctrlBtnMap[i].button) {
                    gba->keysActive |= (1 << ctrlBtnMap[i].bit);
                }
            }
            break;

        case SDL_CONTROLLERBUTTONUP:
            for (size_t i = 0; i < CTRL_BTN_MAP_SIZE; i++) {
                if (event.cbutton.button == ctrlBtnMap[i].button) {
                    gba->keysActive &= ~(1 << ctrlBtnMap[i].bit);
                }
            }
            break;

        case SDL_CONTROLLERAXISMOTION:
            /** Handled after all events are drained. */
            break;

        /** Hot-plug events. */
        case SDL_CONTROLLERDEVICEADDED:
            if (!plat->controller) {
                int idx = event.cdevice.which;
                if (SDL_IsGameController(idx)) {
                    plat->controller = SDL_GameControllerOpen(idx);
                    if (plat->controller) {
                        plat->controllerIndex = idx;
                        printf("CUPID: Controller connected: %s\n",
                               SDL_GameControllerName(plat->controller));
                    }
                }
            }
            break;

        case SDL_CONTROLLERDEVICEREMOVED:
            if (plat->controller &&
                event.cdevice.which == plat->controllerIndex) {
                printf("CUPID: Controller disconnected\n");
                SDL_GameControllerClose(plat->controller);
                plat->controller = NULL;
                plat->controllerIndex = -1;
            }
            break;
        }
    }

    /** Apply analog-stick to d-pad mapping. */
    cupid_input_update_axes(plat, gba);

    /** Test keypad IRQ after key state changes. */
    cupid_gba_test_keypad_irq(gba);

    return true;
}
