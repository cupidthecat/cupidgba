/* SPDX-License-Identifier: GPL-3.0-or-later */
struct MusicProgress {
    struct CupidTimingEvent event;
    struct CupidGBA* gba;
    uint64_t start, first, last, firstTrack, lastTrack, acquired, released;
    uint64_t entries[36];
    unsigned stage, late;
    bool dmaStarted, partial;
};
static void observe_music_progress(struct CupidTiming* timing, void* context, uint32_t late) {
    struct MusicProgress* watch = context;
    struct CupidGBA* gba = watch->gba;
    const uint64_t relative = cupid_timing_global_time(timing) + late - watch->start;
    uint8_t* player = ram(gba, playerAddress);
    uint8_t* tracks = ram(gba, trackAddress);
    if (late) ++watch->late;
    if (watch->stage == 0) {
        if (!watch->first && !word(player, 0)) watch->first = relative;
        if (!watch->last && !word(player, 60)) watch->last = relative;
        if (!watch->firstTrack && !tracks[0]) watch->firstTrack = relative;
        if (!watch->lastTrack && !tracks[15 * 80]) watch->lastTrack = relative;
        if (!watch->acquired && word(ram(gba, soundAddress), 0) == MUSIC_READY + 1) watch->acquired = relative;
        if (watch->acquired && !watch->released && word(player, 52) == MUSIC_READY) watch->released = relative;
    } else if (watch->stage == 1) {
        if (!watch->acquired && word(player, 52) == MUSIC_READY + 1) watch->acquired = relative;
        if (!watch->firstTrack && tracks[0] == 0xC0) watch->firstTrack = relative;
        if (!watch->lastTrack && tracks[15 * 80] == 0xC0) watch->lastTrack = relative;
        if (watch->acquired && !watch->released && word(player, 52) == MUSIC_READY) watch->released = relative;
    } else {
        unsigned written = 0;
        for (unsigned entry = 0; entry < 36; entry++) {
            const uint32_t value = word(gba->memory.ewram, 0x12000 + entry * 4);
            if (value == 0xA5A5A5A5) continue;
            CHECK(written == entry && (value & 1) && value < GBA_SIZE_BIOS);
            ++written;
            if (!watch->entries[entry]) watch->entries[entry] = relative;
        }
        if (written && written < 36) watch->partial = true;
    }
    if (!watch->dmaStarted && relative >= 80) {
        watch->dmaStarted = true;
        cupid_io_write(gba, REG_DMA0SAD_LO, 0x2800);
        cupid_io_write(gba, REG_DMA0SAD_HI, 0x0200);
        cupid_io_write(gba, REG_DMA0DAD_LO, 0x2900);
        cupid_io_write(gba, REG_DMA0DAD_HI, 0x0200);
        cupid_io_write(gba, REG_DMA0CNT_LO, 16);
        cupid_io_write(gba, REG_DMA0CNT_HI, 0xC400);
    }
    cupid_timing_schedule(timing, &watch->event, (int32_t)late + 1);
}
struct MusicProgressResult { uint64_t clocks, chronology, image; };
static struct MusicProgressResult music_partial_service(struct CupidGBA* gba, unsigned stage, bool thumb) {
    struct MusicProgress watch = {.gba = gba, .stage = stage, .start = cupid_timing_global_time(&gba->timing)};
    watch.event = (struct CupidTimingEvent){.callback = observe_music_progress, .context = &watch,
        .priority = 255, .name = "Music bus progress"};
    memset(gba->memory.ewram + 0x12000, 0xA5, 148);
    for (unsigned byte = 0; byte < 64; byte++) gba->memory.ewram[0x2800 + byte] = (uint8_t)(byte * 19 + 7);
    memset(gba->memory.ewram + 0x2900, 0, 64);
    cupid_io_write(gba, REG_IF, 0x3FFF);
    cupid_io_write(gba, REG_TM1CNT_L, 0xFFF0);
    cupid_io_write(gba, REG_TM1CNT_H, 0xC0);
    cupid_timing_schedule(&gba->timing, &watch.event, -5);
    const uint64_t clocks = invoke(gba, stage == 2 ? 0x2A : 0x20 + stage,
        stage == 2 ? 0x02012000 : playerAddress, stage ? 0x02008000 : trackAddress,
        stage ? 0x11220002 : 16, thumb, MODE_IRQ);
    cupid_timing_deschedule(&gba->timing, &watch.event);
    CHECK(watch.late && watch.dmaStarted);
    CHECK(!memcmp(gba->memory.ewram + 0x2800, gba->memory.ewram + 0x2900, 64));
    CHECK((gba->memory.io[REG_IF >> 1] & (IRQ_TIMER1 | IRQ_DMA0)) == (IRQ_TIMER1 | IRQ_DMA0));
    if (!stage) CHECK(watch.first < watch.last && watch.last < watch.firstTrack &&
        watch.firstTrack < watch.lastTrack && watch.lastTrack < watch.acquired && watch.acquired < watch.released);
    if (stage == 1) CHECK(watch.acquired < watch.firstTrack && watch.firstTrack < watch.lastTrack && watch.lastTrack < watch.released);
    if (stage == 2) {
        CHECK(watch.partial && watch.entries[0] && watch.entries[35]);
        for (unsigned entry = 1; entry < 36; entry++) CHECK(watch.entries[entry - 1] < watch.entries[entry]);
        CHECK(word(gba->memory.ewram, 0x12090) == 0xA5A5A5A5);
    }
    uint64_t signature = UINT64_C(14695981039346656037);
    const uint64_t fields[] = {watch.first, watch.last, watch.firstTrack, watch.lastTrack, watch.acquired, watch.released};
    for (unsigned field = 0; field < 42; field++) {
        const uint64_t value = field < 6 ? fields[field] : watch.entries[field - 6];
        for (unsigned byte = 0; byte < 8; byte++) signature = (signature ^ (uint8_t)(value >> (byte * 8))) * UINT64_C(1099511628211);
    }
    return (struct MusicProgressResult){clocks, signature, image_signature(gba)};
}
