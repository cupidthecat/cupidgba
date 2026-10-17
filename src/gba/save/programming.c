/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"

static uint64_t now(const struct CupidSavedata* save) {
    return save->timing ? cupid_timing_global_time(save->timing) : save->localClock;
}

static void complete(struct CupidSavedata* save) {
    if (!save->programming) return;
    if (save->erasing) memset(save->data + save->programOffset, 0xFF, save->programLength);
    else memcpy(save->data + save->programOffset, save->programData, save->programLength);
    save->dirty = true;
    save->programming = false;
    save->pollToggle = false;
}

static void ready(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing;
    (void)late;
    complete(context);
}

static void schedule(struct CupidSavedata* save) {
    if (save->timing && save->programming) {
        cupid_timing_schedule_absolute(save->timing, &save->readyEvent,
                                      (int32_t)(uint32_t)save->readyClock);
    }
}

void cupid_savedata_programming_init(struct CupidSavedata* save) {
    save->readyEvent = (struct CupidTimingEvent){.context = save,
        .callback = ready, .name = "Save programming ready", .priority = 0};
    /* These deterministic profile values are estimates, not chip calibration. */
    save->eepromProgramClocks = 108368;
    save->flashProgramClocks = 650;
    save->flashSectorEraseClocks = 30000;
    save->flashChipEraseClocks = 30000;
    save->flashProfile = FLASH_PROFILE_DEFAULT;
}

static uint32_t microseconds_to_clocks(uint32_t microseconds) {
    return (uint32_t)((UINT64_C(16777216) * microseconds + 999999) / 1000000);
}

bool cupid_savedata_set_flash_profile(struct CupidSavedata* save,
                                     enum CupidFlashProfile profile) {
    if ((profile != FLASH_PROFILE_DEFAULT && profile != FLASH_PROFILE_SST39VF512) ||
        (save->type != SAVE_FLASH512 && save->type != SAVE_FLASH1M) ||
        !save->data || save->programming || save->flashState != FLASH_STATE_RAW ||
        save->command != FLASH_COMMAND_NONE) return false;
    if (profile == FLASH_PROFILE_SST39VF512 &&
        (save->type != SAVE_FLASH512 || save->size != GBA_SIZE_FLASH512 || save->flashBank))
        return false;
    save->flashProfile = profile;
    if (profile == FLASH_PROFILE_SST39VF512) {
        /* Nominal typical durations, rounded up to whole GBA clocks. */
        save->flashProgramClocks = microseconds_to_clocks(14);
        save->flashSectorEraseClocks = microseconds_to_clocks(18000);
        save->flashChipEraseClocks = microseconds_to_clocks(70000);
        save->typeForced = true;
    } else {
        save->flashProgramClocks = 650;
        save->flashSectorEraseClocks = 30000;
        save->flashChipEraseClocks = 30000;
    }
    return true;
}

bool cupid_savedata_set_programming_clocks(struct CupidSavedata* save,
                                         uint32_t eeprom, uint32_t byte,
                                         uint32_t sector, uint32_t chip) {
    if (save->programming || !eeprom || !byte || !sector || !chip ||
        eeprom > INT32_MAX || byte > INT32_MAX ||
        sector > INT32_MAX || chip > INT32_MAX) return false;
    save->eepromProgramClocks = eeprom;
    save->flashProgramClocks = byte;
    save->flashSectorEraseClocks = sector;
    save->flashChipEraseClocks = chip;
    return true;
}

void cupid_savedata_bind_timing(struct CupidSavedata* save, struct CupidTiming* timing) {
    uint64_t remaining = 0;
    if (save->programming) {
        const uint64_t clock = now(save);
        if (clock >= save->readyClock) complete(save);
        else remaining = save->readyClock - clock;
    }
    if (save->timing) cupid_timing_deschedule(save->timing, &save->readyEvent);
    save->timing = timing;
    if (save->programming) save->readyClock = now(save) + remaining;
    schedule(save);
}

void cupid_savedata_cancel_programming(struct CupidSavedata* save) {
    if (save->timing) cupid_timing_deschedule(save->timing, &save->readyEvent);
    save->programming = false;
    save->pollToggle = false;
}

void cupid_savedata_begin_programming(struct CupidSavedata* save, uint32_t offset,
                                     uint32_t length, const uint8_t* data, uint32_t clocks) {
    if (save->programming || !clocks || clocks > INT32_MAX || !length || offset > save->size ||
        length > save->size - offset || (data && length > sizeof(save->programData))) return;
    save->programOffset = offset;
    save->programLength = length;
    save->erasing = data == NULL;
    if (data) memcpy(save->programData, data, length);
    save->pollToggle = false;
    save->programming = true;
    save->readyClock = now(save) + clocks;
    schedule(save);
}

void cupid_savedata_advance_clock(struct CupidSavedata* save, uint64_t clocks) {
    if (!save->timing) {
        save->localClock += clocks;
        if (save->programming && save->localClock >= save->readyClock) complete(save);
        return;
    }
    /* Explicit elapsed time while the system clock is stopped. No timer,
     * DMA, video, audio or CPU clock advances with the cartridge operation. */
    if (!save->programming || !clocks) return;
    cupid_timing_deschedule(save->timing, &save->readyEvent);
    const uint64_t clock = now(save);
    const uint64_t remaining = save->readyClock > clock ? save->readyClock - clock : 0;
    if (clocks >= remaining) complete(save);
    else { save->readyClock = clock + remaining - clocks; schedule(save); }
}
