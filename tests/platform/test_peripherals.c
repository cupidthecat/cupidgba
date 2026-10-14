/* SPDX-License-Identifier: GPL-3.0-or-later */
#define SDL_MAIN_HANDLED
#include "cupid/platform.h"
#include "cupid/platform/peripherals.h"
#include "../test.h"

static void configuration_and_strict_input_parsing(void) {
    int mode = 9;
    CHECK(cupid_platform_peripheral_mode("auto", &mode) && mode == -1);
    CHECK(cupid_platform_peripheral_mode("on", &mode) && mode == 1);
    CHECK(cupid_platform_peripheral_mode("off", &mode) && mode == 0);
    CHECK(!cupid_platform_peripheral_mode("ON", &mode) && mode == 0);
    uint8_t level = 19;
    const char* invalid[] = {"", "-1", "+1", "256", "1.5", " 1", "1 ", "0x40", "999999999999999999999"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        CHECK(!cupid_platform_solar_level(invalid[i], &level) && level == 19);
    }
    for (unsigned value = 0; value < 256; value++) {
        char text[4];
        snprintf(text, sizeof(text), "%u", value);
        CHECK(cupid_platform_solar_level(text, &level) && level == value);
    }
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_cartridge_enable_rtc(gba, true);
    const struct CupidPlatformPeripherals config = {1, 1, 73};
    cupid_platform_peripherals_configure(gba, &config);
    cupid_gba_reset(gba);
    CHECK(gba->cartridge.solarPresent && gba->cartridge.rumblePresent && gba->cartridge.rtcPresent);
    CHECK(gba->cartridge.solar.threshold == 182);
    const struct CupidPlatformPeripherals automatic = {-1, -1, 255};
    cupid_platform_peripherals_configure(gba, &automatic);
    CHECK(gba->cartridge.solarPresent && gba->cartridge.rumblePresent && !gba->cartridge.solar.threshold);
    const struct CupidPlatformPeripherals disabled = {0, 0, 0};
    cupid_platform_peripherals_configure(gba, &disabled);
    CHECK(!gba->cartridge.solarPresent && !gba->cartridge.rumblePresent && gba->cartridge.rtcPresent);
    cupid_gba_destroy(gba);
    free(gba);
}

struct HostMotor { unsigned on, off; uint16_t low, high; };
static int SDLCALL host_motor(void* context, uint16_t low, uint16_t high) {
    struct HostMotor* motor = context;
    motor->low = low; motor->high = high;
    if (low || high) motor->on++;
    else motor->off++;
    return 0;
}

static void controller_event(uint32_t type, int which) {
    SDL_Event event = {0};
    event.type = type;
    event.cdevice.which = which;
    CHECK(SDL_PushEvent(&event) == 1);
}

static void virtual_controller_output_hotplug_expiry_and_shutdown(void) {
    SDL_SetMainReady();
    CHECK(SDL_Init(SDL_INIT_GAMECONTROLLER) == 0);
    /* Consume an instance ID so it differs from the subsequent device index. */
    int unused = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_UNKNOWN, 0, 1, 0);
    CHECK(unused >= 0 && SDL_JoystickDetachVirtual(unused) == 0);
    struct HostMotor motor = {0};
    SDL_VirtualJoystickDesc desc = {0};
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    desc.button_mask = 1u << SDL_CONTROLLER_BUTTON_A;
    desc.axis_mask = 1u << SDL_CONTROLLER_AXIS_LEFTX;
    desc.name = "CupidGBA regression controller";
    desc.userdata = &motor;
    desc.Rumble = host_motor;
    const int index = SDL_JoystickAttachVirtualEx(&desc);
    CHECK(index >= 0 && SDL_IsGameController(index));
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    gba->memory.rom = calloc(1, 0x4000);
    CHECK(gba->memory.rom);
    gba->memory.romSize = 0x4000;
    gba->memory.romMask = 0x3FFF;
    cupid_gba_reset(gba);
    cupid_cartridge_enable_rumble(gba, true);
    struct CupidPlatform plat = {.controllerIndex = -1, .running = true};
    CHECK(cupid_platform_poll_events(&plat, gba) && plat.controller);
    CHECK(plat.controllerIndex == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(plat.controller)));
    CHECK(plat.controllerIndex != index);
    cupid_cartridge_enable_solar(gba, true);
    cupid_cartridge_set_solar_level(gba, 250);
    SDL_Event key = {0};
    key.type = SDL_KEYDOWN;
    key.key.keysym.scancode = SDL_SCANCODE_F6;
    CHECK(SDL_PushEvent(&key) == 1 && cupid_platform_poll_events(&plat, gba));
    CHECK(gba->cartridge.solar.threshold == 0);
    key.key.keysym.scancode = SDL_SCANCODE_F5;
    CHECK(SDL_PushEvent(&key) == 1 && cupid_platform_poll_events(&plat, gba));
    CHECK(gba->cartridge.solar.threshold == 16);
    cupid_cartridge_set_solar_level(gba, 4);
    CHECK(SDL_PushEvent(&key) == 1 && cupid_platform_poll_events(&plat, gba));
    CHECK(gba->cartridge.solar.threshold == 255);
    cupid_cartridge_set_rumble_output(gba, cupid_platform_rumble_changed, &plat);
    cupid_memory_store16(&gba->cpu, 0x080000C6, 8, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C4, 8, NULL);
    CHECK(plat.rumbleActive && plat.rumbleDriving && motor.low == 0x8000 && motor.high == 0x8000);
    const unsigned on = motor.on;
    cupid_memory_store16(&gba->cpu, 0x080000C4, 8, NULL);
    CHECK(motor.on == on);
    SDL_Delay(280);
    SDL_JoystickUpdate();
    CHECK(!motor.low && !motor.high); /* The finite host effect expires. */
    cupid_platform_rumble_refresh(&plat);
    CHECK(motor.low == 0x8000 && motor.high == 0x8000);
    controller_event(SDL_CONTROLLERDEVICEREMOVED, plat.controllerIndex);
    CHECK(cupid_platform_poll_events(&plat, gba));
    CHECK(!plat.controller && !plat.rumbleDriving && !motor.low && !motor.high);
    CHECK(plat.rumbleActive && gba->cartridge.rumbleActive);
    controller_event(SDL_CONTROLLERDEVICEADDED, index);
    CHECK(cupid_platform_poll_events(&plat, gba) && plat.controller);
    CHECK(motor.low && motor.high);
    cupid_gba_reset(gba);
    CHECK(!plat.rumbleActive && !motor.low && !motor.high);
    cupid_memory_store16(&gba->cpu, 0x080000C6, 8, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C4, 8, NULL);
    CHECK(motor.low && motor.high);
    cupid_cartridge_set_rumble_output(gba, NULL, NULL);
    CHECK(!plat.rumbleActive && !motor.low && !motor.high && motor.off >= 3);
    cupid_platform_rumble_stop(&plat);
    SDL_GameControllerClose(plat.controller);
    CHECK(SDL_JoystickDetachVirtual(index) == 0);
    plat = (struct CupidPlatform){.controllerIndex = -1, .running = true};
    desc.Rumble = NULL;
    const int unsupported = SDL_JoystickAttachVirtualEx(&desc);
    CHECK(unsupported >= 0);
    CHECK(cupid_platform_poll_events(&plat, gba) && plat.controller);
    CHECK(SDL_GameControllerRumble(plat.controller, 1, 1, 1) == -1);
    cupid_cartridge_set_rumble_output(gba, cupid_platform_rumble_changed, &plat);
    CHECK(plat.rumbleActive && gba->cartridge.rumbleActive);
    cupid_gba_reset(gba);
    CHECK(!plat.rumbleActive && !gba->cartridge.rumbleActive);
    cupid_cartridge_set_rumble_output(gba, NULL, NULL);
    SDL_GameControllerClose(plat.controller);
    CHECK(SDL_JoystickDetachVirtual(unsupported) == 0);
    cupid_gba_destroy(gba);
    free(gba);
    SDL_Quit();
}

static void an_existing_controller_inherits_active_motor_output(void) {
    SDL_SetMainReady();
    CHECK(SDL_Init(SDL_INIT_GAMECONTROLLER) == 0);
    struct HostMotor first = {0}, second = {0};
    SDL_VirtualJoystickDesc desc = {0};
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    desc.name = "CupidGBA fallback controller";
    desc.Rumble = host_motor;
    desc.userdata = &first;
    const int firstIndex = SDL_JoystickAttachVirtualEx(&desc);
    CHECK(firstIndex >= 0);
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    gba->memory.rom = calloc(1, 0x4000);
    CHECK(gba->memory.rom);
    gba->memory.romSize = 0x4000;
    gba->memory.romMask = 0x3FFF;
    cupid_gba_reset(gba);
    cupid_cartridge_enable_rumble(gba, true);
    struct CupidPlatform plat = {.controllerIndex = -1, .running = true};
    CHECK(cupid_platform_poll_events(&plat, gba) && plat.controller);
    const int firstId = plat.controllerIndex;
    desc.userdata = &second;
    const int secondIndex = SDL_JoystickAttachVirtualEx(&desc);
    CHECK(secondIndex >= 0);
    CHECK(cupid_platform_poll_events(&plat, gba) && plat.controllerIndex == firstId);
    cupid_cartridge_set_rumble_output(gba, cupid_platform_rumble_changed, &plat);
    cupid_memory_store16(&gba->cpu, 0x080000C6, 8, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C4, 8, NULL);
    CHECK(first.low && first.high && !second.low && !second.high);
    controller_event(SDL_CONTROLLERDEVICEREMOVED, firstId);
    CHECK(cupid_platform_poll_events(&plat, gba) && plat.controller);
    CHECK(plat.controllerIndex == SDL_JoystickGetDeviceInstanceID(secondIndex));
    CHECK(!first.low && !first.high && second.low && second.high);
    CHECK(plat.rumbleActive && gba->cartridge.rumbleActive);
    cupid_cartridge_set_rumble_output(gba, NULL, NULL);
    CHECK(!second.low && !second.high);
    SDL_GameControllerClose(plat.controller);
    CHECK(SDL_JoystickDetachVirtual(secondIndex) == 0);
    CHECK(SDL_JoystickDetachVirtual(firstIndex) == 0);
    cupid_gba_destroy(gba);
    free(gba);
    SDL_Quit();
}

int main(void) {
    RUN(configuration_and_strict_input_parsing);
    RUN(virtual_controller_output_hotplug_expiry_and_shutdown);
    RUN(an_existing_controller_inherits_active_motor_output);
    return 0;
}
