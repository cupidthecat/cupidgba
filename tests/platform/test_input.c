/* SPDX-License-Identifier: GPL-3.0-or-later */
#define SDL_MAIN_HANDLED
#include "cupid/platform.h"
#include "../test.h"

static const SDL_Scancode keys[10] = {
    SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_RETURN,
    SDL_SCANCODE_RIGHT, SDL_SCANCODE_LEFT, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
    SDL_SCANCODE_S, SDL_SCANCODE_A
};
static const SDL_GameControllerButton buttons[10] = {
    SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_BACK,
    SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_UP,
    SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER
};

struct Fixture { struct CupidGBA* gba; struct CupidPlatform platform; SDL_Joystick* joystick; SDL_JoystickID id; };

static int attach(void) {
    SDL_VirtualJoystickDesc desc = {0};
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    desc.button_mask = (1u << SDL_CONTROLLER_BUTTON_MAX) - 1;
    desc.axis_mask = (1u << SDL_CONTROLLER_AXIS_MAX) - 1;
    desc.vendor_id = 0xFFFF;
    desc.product_id = 0xCAFE;
    desc.name = "CupidGBA input regression";
    const int index = SDL_JoystickAttachVirtualEx(&desc);
    CHECK(index >= 0 && SDL_IsGameController(index));
    return index;
}

static void detach(SDL_JoystickID id) {
    for (int index = 0; index < SDL_NumJoysticks(); index++) {
        if (SDL_JoystickGetDeviceInstanceID(index) == id) {
            CHECK(SDL_JoystickIsVirtual(index) && SDL_JoystickDetachVirtual(index) == 0);
            return;
        }
    }
}

static void poll(struct Fixture* f) {
    SDL_JoystickUpdate();
    CHECK(cupid_platform_poll_events(&f->platform, f->gba));
}

static struct Fixture fixture(void) {
    SDL_SetMainReady();
    CHECK(SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT, "0xFFFF/0xCAFE") == SDL_TRUE);
    CHECK(SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1") == SDL_TRUE);
    CHECK(SDL_Init(SDL_INIT_GAMECONTROLLER) == 0);
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    /* Give the selected device an instance ID distinct from its device index. */
    const int unused = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_UNKNOWN, 0, 1, 0);
    CHECK(unused >= 0 && SDL_JoystickDetachVirtual(unused) == 0);
    const int index = attach();
    struct Fixture f = {.platform = {.running = true, .controllerIndex = -1}};
    f.joystick = SDL_JoystickOpen(index);
    CHECK(f.joystick);
    f.id = SDL_JoystickInstanceID(f.joystick);
    CHECK(f.id != index);
    f.gba = calloc(1, sizeof(*f.gba));
    CHECK(f.gba);
    cupid_gba_create(f.gba);
    cupid_gba_reset(f.gba);
    poll(&f);
    CHECK(f.platform.controller && SDL_GameControllerGetJoystick(f.platform.controller) == f.joystick);
    return f;
}

static void finish(struct Fixture* f) {
    if (f->platform.controller) SDL_GameControllerClose(f->platform.controller);
    SDL_JoystickClose(f->joystick);
    detach(f->id);
    cupid_gba_destroy(f->gba);
    free(f->gba);
    SDL_Quit();
}

static void key(SDL_Scancode code, bool down) {
    SDL_Event event = {0};
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.keysym.scancode = code;
    CHECK(SDL_PushEvent(&event) == 1);
}

static void control(SDL_Joystick* joystick, unsigned bit, bool down) {
    CHECK(bit < 10);
    CHECK(SDL_JoystickSetVirtualButton(joystick, buttons[bit], down) == 0);
}

static void expect(struct Fixture* f, uint16_t pressed) {
    CHECK(f->gba->keysActive == pressed);
    CHECK(cupid_io_read(f->gba, REG_KEYINPUT) == (0x3FF ^ pressed));
}

static void all_bindings_retain_digital_directions(void) {
    struct Fixture f = fixture();
    unsigned complete = 0;
    for (unsigned bit = 0; bit < 10; bit++) {
        key(keys[bit], true); poll(&f); expect(&f, (uint16_t)(1u << bit));
        poll(&f); expect(&f, (uint16_t)(1u << bit));
        key(keys[bit], false); poll(&f); expect(&f, 0);
        control(f.joystick, bit, true); poll(&f); expect(&f, (uint16_t)(1u << bit));
        poll(&f); expect(&f, (uint16_t)(1u << bit));
        control(f.joystick, bit, false); poll(&f); expect(&f, 0);
        complete++;
    }
    CHECK(complete == 10);
    finish(&f);
}

static void release_order_and_complete_mixed_masks(void) {
    struct Fixture f = fixture();
    for (unsigned bit = 0; bit < 10; bit++) {
        const uint16_t pressed = (uint16_t)(1u << bit);
        key(keys[bit], true); control(f.joystick, bit, true);
        poll(&f); expect(&f, pressed);
        key(keys[bit], false); poll(&f); expect(&f, pressed);
        control(f.joystick, bit, false); poll(&f); expect(&f, 0);
        key(keys[bit], true); control(f.joystick, bit, true);
        poll(&f); expect(&f, pressed);
        control(f.joystick, bit, false); poll(&f); expect(&f, pressed);
        key(keys[bit], false); poll(&f); expect(&f, 0);
    }
    unsigned complete = 0;
    for (unsigned mask = 0; mask < 1024; mask++) {
        const uint16_t controller = (uint16_t)(mask ^ 0x3FF);
        for (unsigned bit = 0; bit < 10; bit++) {
            key(keys[bit], (mask & (1u << bit)) != 0);
            control(f.joystick, bit, (controller & (1u << bit)) != 0);
        }
        poll(&f); expect(&f, 0x3FF);
        for (unsigned bit = 0; bit < 10; bit++) key(keys[bit], false);
        poll(&f); expect(&f, controller);
        for (unsigned bit = 0; bit < 10; bit++) control(f.joystick, bit, false);
        poll(&f); expect(&f, 0);
        complete++;
    }
    CHECK(complete == 1024);
    finish(&f);
}

static void axes_preserve_other_sources_and_strict_dead_zone(void) {
    const int16_t values[] = {-32768, -16385, -16384, -1, 0, 1, 16384, 16385, 32767};
    const uint16_t horizontal[] = {32, 32, 0, 0, 0, 0, 0, 16, 16};
    const uint16_t vertical[] = {64, 64, 0, 0, 0, 0, 0, 128, 128};
    struct Fixture f = fixture();
    key(SDL_SCANCODE_RIGHT, true);
    control(f.joystick, 7, true);
    unsigned complete = 0;
    for (unsigned x = 0; x < 9; x++) {
        for (unsigned y = 0; y < 9; y++) {
            CHECK(SDL_JoystickSetVirtualAxis(f.joystick, SDL_CONTROLLER_AXIS_LEFTX, values[x]) == 0);
            CHECK(SDL_JoystickSetVirtualAxis(f.joystick, SDL_CONTROLLER_AXIS_LEFTY, values[y]) == 0);
            poll(&f);
            CHECK(SDL_GameControllerGetAxis(f.platform.controller, SDL_CONTROLLER_AXIS_LEFTX) == values[x]);
            CHECK(SDL_GameControllerGetAxis(f.platform.controller, SDL_CONTROLLER_AXIS_LEFTY) == values[y]);
            expect(&f, (uint16_t)(0x90 | horizontal[x] | vertical[y]));
            complete++;
        }
    }
    CHECK(complete == 81);
    finish(&f);
}

static void selected_instance_focus_detach_and_reconnect(void) {
    struct Fixture f = fixture();
    CHECK(f.platform.controllerIndex == f.id);
    const int second = attach();
    SDL_Joystick* other = SDL_JoystickOpen(second);
    CHECK(other);
    const SDL_JoystickID otherId = SDL_JoystickInstanceID(other);
    control(other, 1, true); poll(&f); expect(&f, 0);
    key(SDL_SCANCODE_RIGHT, true); control(f.joystick, 0, true);
    poll(&f); expect(&f, 17);
    SDL_Event focus = {0};
    focus.type = SDL_WINDOWEVENT;
    focus.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    CHECK(SDL_PushEvent(&focus) == 1);
    poll(&f); expect(&f, 1);
    key(SDL_SCANCODE_LEFT, true); poll(&f); expect(&f, 33);
    detach(f.id); poll(&f);
    CHECK(f.platform.controllerIndex == otherId);
    expect(&f, 34); /* Existing secondary controller becomes the selected owner. */
    detach(otherId); poll(&f);
    CHECK(!f.platform.controller && f.platform.controllerIndex == -1);
    expect(&f, 32);
    SDL_JoystickClose(other);
    const int reconnected = attach();
    other = SDL_JoystickOpen(reconnected);
    CHECK(other);
    control(other, 2, true); poll(&f);
    CHECK(f.platform.controllerIndex == SDL_JoystickInstanceID(other));
    expect(&f, 36);
    key(SDL_SCANCODE_LEFT, false); poll(&f); expect(&f, 4);
    if (f.platform.controller) SDL_GameControllerClose(f.platform.controller);
    f.platform.controller = NULL;
    detach(SDL_JoystickInstanceID(other));
    SDL_JoystickClose(other);
    finish(&f);
}

static void combined_input_reaches_keypad_irq_and_wakes_stop(void) {
    struct Fixture f = fixture();
    cupid_io_write(f.gba, REG_KEYCNT, 0xC011); /* AND: A and Right. */
    key(SDL_SCANCODE_Z, true); poll(&f); expect(&f, 1);
    CHECK(!((f.gba->irqf | f.gba->irq.pendingIF) & IRQ_KEYPAD));
    control(f.joystick, 4, true); poll(&f); expect(&f, 17);
    CHECK((f.gba->irqf | f.gba->irq.pendingIF) & IRQ_KEYPAD);
    cupid_timing_tick(&f.gba->timing, 1);
    CHECK(f.gba->irqf & IRQ_KEYPAD);
    finish(&f);
    f = fixture();
    cupid_io_write(f.gba, REG_IE, IRQ_KEYPAD);
    cupid_io_write(f.gba, REG_IME, 0);
    cupid_io_write(f.gba, REG_KEYCNT, 0x4010);
    cupid_timing_tick(&f.gba->timing, 1);
    f.gba->cpu.cpsr.i = 1;
    cupid_gba_stop(f.gba);
    CHECK(f.gba->stopped);
    const uint64_t clock = cupid_timing_global_time(&f.gba->timing);
    key(SDL_SCANCODE_RIGHT, true); poll(&f); expect(&f, 16);
    CHECK(!f.gba->stopped && cupid_timing_global_time(&f.gba->timing) == clock);
    CHECK((f.gba->irqf | f.gba->irq.pendingIF) & IRQ_KEYPAD);
    finish(&f);
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "bindings")) RUN(all_bindings_retain_digital_directions);
    else if (!strcmp(argv[1], "mixed")) RUN(release_order_and_complete_mixed_masks);
    else if (!strcmp(argv[1], "axes")) RUN(axes_preserve_other_sources_and_strict_dead_zone);
    else if (!strcmp(argv[1], "lifecycle")) RUN(selected_instance_focus_detach_and_reconnect);
    else if (!strcmp(argv[1], "irq")) RUN(combined_input_reaches_keypad_irq_and_wakes_stop);
    else return 2;
    return 0;
}
