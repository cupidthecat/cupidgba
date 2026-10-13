/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

static struct CupidGBA* machine(uint64_t origin) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = origin;
    gba->timing.masterCycles = (uint32_t)origin;
    gba->audio.lastSample = origin;
    gba->audio.sampleOrigin = origin;
    gba->audio.nextSample = origin + 512;
    cupid_timing_schedule(&gba->timing, &gba->audio.sampleEvent, 512);
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void tick(struct CupidGBA* gba, int clocks) {
    cupid_timing_tick(&gba->timing, clocks);
}

static void select_cpu_bank(struct CupidGBA* gba, unsigned bank) {
    cupid_io_write(gba, REG_SOUND3CNT_L, bank ? 0 : 0x40);
}

static uint8_t byte_value(unsigned bank, unsigned offset) {
    return (uint8_t)(0x31 + bank * 0x40 + offset * 3);
}

static void wave_banks_use_the_full_cpu_window_at_each_width(void) {
    for (unsigned width = 1; width <= 4; width *= 2) {
        struct CupidGBA* gba = machine(0);
        cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
        for (unsigned bank = 0; bank < 2; bank++) {
            select_cpu_bank(gba, bank);
            for (unsigned i = 0; i < 16; i += width) {
                uint32_t value = 0;
                for (unsigned j = 0; j < width; j++) value |= (uint32_t)byte_value(bank, i + j) << (8 * j);
                uint32_t address = GBA_BASE_IO + REG_WAVE_RAM0_LO + i;
                if (width == 1) cupid_memory_store8(&gba->cpu, address, (uint8_t)value, NULL);
                else if (width == 2) cupid_memory_store16(&gba->cpu, address, (uint16_t)value, NULL);
                else cupid_memory_store32(&gba->cpu, address, (int32_t)value, NULL);
            }
        }
        for (unsigned bank = 0; bank < 2; bank++) {
            select_cpu_bank(gba, bank);
            for (unsigned i = 0; i < 16; i++) {
                CHECK(gba->audio.psg.ch3.wavedata8[bank * 16 + i] == byte_value(bank, i));
                CHECK(cupid_memory_load8(&gba->cpu, GBA_BASE_IO + REG_WAVE_RAM0_LO + i, NULL) == byte_value(bank, i));
            }
            for (unsigned i = 0; i < 16; i += 2) {
                uint16_t expected = byte_value(bank, i) | ((uint16_t)byte_value(bank, i + 1) << 8);
                CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_IO + REG_WAVE_RAM0_LO + i, NULL) == expected);
            }
            for (unsigned i = 0; i < 16; i += 4) {
                uint32_t expected = 0;
                for (unsigned j = 0; j < 4; j++) expected |= (uint32_t)byte_value(bank, i + j) << (8 * j);
                CHECK((uint32_t)cupid_memory_load32(&gba->cpu, GBA_BASE_IO + REG_WAVE_RAM0_LO + i, NULL) == expected);
            }
        }
        select_cpu_bank(gba, 0);
        cupid_memory_store8(&gba->cpu, GBA_BASE_IO + 0x09F, 0xE7, NULL);
        CHECK(cupid_io_read(gba, 0x09E) == (byte_value(0, 14) | 0xE700));
        select_cpu_bank(gba, 1);
        CHECK(cupid_io_read(gba, 0x09E) == (byte_value(1, 14) | ((uint16_t)byte_value(1, 15) << 8)));
        destroy(gba);
    }
}

static void powered_off_mode_writes_leave_wave_ram_accessible(void) {
    struct CupidGBA* gba = machine(0);
    cupid_memory_store32(&gba->cpu, GBA_BASE_IO + REG_SOUND1CNT_H, -1, NULL);
    cupid_memory_store8(&gba->cpu, GBA_BASE_IO + REG_SOUND3CNT_L, 0xFF, NULL);
    cupid_memory_store16(&gba->cpu, GBA_BASE_IO + REG_SOUNDCNT_L, 0xFFFF, NULL);
    CHECK(cupid_io_read(gba, REG_SOUND1CNT_H) == 0);
    CHECK(cupid_io_read(gba, REG_SOUND3CNT_L) == 0);
    CHECK(cupid_io_read(gba, REG_SOUNDCNT_L) == 0);
    cupid_memory_store32(&gba->cpu, GBA_BASE_IO + 0x09C, 0x76543210, NULL);
    CHECK(gba->audio.psg.ch3.wavedata32[7] == 0x76543210);
    CHECK((uint32_t)cupid_memory_load32(&gba->cpu, GBA_BASE_IO + 0x09C, NULL) == 0x76543210);
    for (unsigned i = 0; i < 16; i++) CHECK(gba->audio.psg.ch3.wavedata8[i] == 0);
    destroy(gba);
}

static void square_bytes_preserve_running_envelopes_and_lengths(void) {
    for (unsigned channel = 0; channel < 2; channel++) {
        for (unsigned thumb = 0; thumb < 2; thumb++) {
            struct CupidGBA* gba = machine(0);
            cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
            cupid_io_write(gba, REG_SOUND1CNT_L, 0);
            uint32_t envelope = channel ? REG_SOUND2CNT_L : REG_SOUND1CNT_H;
            uint32_t frequency = channel ? REG_SOUND2CNT_H : REG_SOUND1CNT_X;
            struct CupidAudioSquareChannel* square = channel ? &gba->audio.psg.ch2 : &gba->audio.psg.ch1;
            cupid_io_write(gba, envelope, 0xC182);
            cupid_io_write(gba, frequency, 0xC700);
            for (unsigned i = 0; i < 8; i++) cupid_audio_psg_frame_step(&gba->audio.psg);
            CHECK(square->envelope.currentVolume == 11 && square->control.length == 58);
            _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
            const uint32_t store = thumb ? 0x7001 : 0xE5C01000; /* STRB r1,[r0] */
            memcpy(gba->memory.iwram, &store, sizeof(store));
            for (unsigned field = 0; field < 2; field++) {
                gba->cpu.gprs[0] = GBA_BASE_IO + (field ? envelope + 1 : frequency);
                gba->cpu.gprs[1] = field ? 0xC2 : 0x34;
                gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
                if (thumb) { CupidThumbWritePC(&gba->cpu); cupid_thumb_step(&gba->cpu); }
                else { CupidARMWritePC(&gba->cpu); cupid_arm_step(&gba->cpu); }
                CHECK(square->envelope.currentVolume == 11);
                CHECK(square->control.length == 58);
            }
            CHECK(square->control.frequency == 0x734 && square->envelope.stepTime == 2);
            cupid_memory_store8(&gba->cpu, GBA_BASE_IO + frequency + 1, 0xC7, NULL);
            CHECK(square->envelope.currentVolume == 12); /* An addressed trigger still acts. */
            destroy(gba);
        }
    }
}

static void wave_and_noise_bytes_do_not_repeat_other_fields(void) {
    struct CupidGBA* gba = machine(0);
    cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
    cupid_io_write(gba, REG_SOUND3CNT_L, 0x80);
    cupid_io_write(gba, REG_SOUND3CNT_H, 0x2002);
    cupid_io_write(gba, REG_SOUND3CNT_X, 0xC7FF);
    cupid_io_write(gba, REG_SOUND4CNT_L, 0xC102);
    cupid_io_write(gba, REG_SOUND4CNT_H, 0xC000);
    for (unsigned i = 0; i < 8; i++) cupid_audio_psg_frame_step(&gba->audio.psg);
    CHECK(gba->audio.psg.ch3.length == 250 && gba->audio.psg.ch4.length == 58);
    uint64_t waveDeadline = gba->audio.psg.ch3.nextUpdate;
    cupid_memory_store8(&gba->cpu, GBA_BASE_IO + REG_SOUND3CNT_X, 0xFE, NULL);
    CHECK(gba->audio.psg.ch3.nextUpdate == waveDeadline);
    cupid_memory_store8(&gba->cpu, GBA_BASE_IO + REG_SOUND3CNT_H + 1, 0x40, NULL);
    CHECK(gba->audio.psg.ch3.length == 250 && gba->audio.psg.ch3.volume == 2);
    CHECK(gba->audio.psg.ch4.envelope.currentVolume == 11);
    cupid_memory_store8(&gba->cpu, GBA_BASE_IO + REG_SOUND4CNT_H, 0x13, NULL);
    CHECK(gba->audio.psg.ch4.envelope.currentVolume == 11);
    cupid_memory_store8(&gba->cpu, GBA_BASE_IO + REG_SOUND4CNT_L + 1, 0xC2, NULL);
    CHECK(gba->audio.psg.ch4.length == 58);
    cupid_memory_store8(&gba->cpu, GBA_BASE_IO + REG_SOUND3CNT_L + 1, 0xFF, NULL);
    cupid_memory_store8(&gba->cpu, GBA_BASE_IO + REG_SOUNDCNT_X + 1, 0, NULL);
    CHECK(gba->audio.psg.ch3.enable && gba->audio.enable);
    destroy(gba);
}

static void active_wave_fetches_preserve_the_other_bank(void) {
    struct CupidGBA* gba = machine(0);
    cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
    for (unsigned bank = 0; bank < 2; bank++) {
        select_cpu_bank(gba, bank);
        for (unsigned i = 0; i < 16; i++) cupid_memory_store8(&gba->cpu, GBA_BASE_IO + 0x90 + i, bank ? 0xBB : 0xAA, NULL);
    }
    cupid_io_write(gba, REG_SOUND3CNT_L, 0x80); /* Bank 0 plays; CPU accesses bank 1. */
    cupid_io_write(gba, REG_SOUND3CNT_H, 0x2000);
    cupid_io_write(gba, REG_SOUND3CNT_X, 0x87FF);
    tick(gba, 63);
    cupid_memory_store8(&gba->cpu, GBA_BASE_IO + 0x09F, 0xD3, NULL);
    CHECK(gba->audio.psg.ch3.sample == 10);
    CHECK(cupid_io_read(gba, 0x09E) == 0xD3BB);
    for (unsigned i = 0; i < 16; i++) CHECK(gba->audio.psg.ch3.wavedata8[i] == 0xAA);
    tick(gba, 449);
    CHECK(gba->audio.psg.ch3.sample == 10 && gba->audio.lastSample == 512);
    destroy(gba);
}

static void wave_nibble_order_and_complete_cycles_preserve_data(void) {
    for (unsigned dimension = 0; dimension < 2; dimension++) {
        for (unsigned bank = 0; bank < 2; bank++) {
            struct CupidGBA* gba = machine(0);
            cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
            for (unsigned target = 0; target < 2; target++) {
                select_cpu_bank(gba, target);
                for (unsigned byte = 0; byte < 16; byte++) {
                    unsigned step = target * 32 + byte * 2;
                    uint8_t packed = (uint8_t)(((step * 3 + 4 + target * 5) & 15) << 4) |
                                     (uint8_t)(((step + 1) * 3 + 4 + target * 5) & 15);
                    cupid_memory_store8(&gba->cpu, GBA_BASE_IO + 0x90 + byte, packed, NULL);
                }
            }
            uint8_t initial[32];
            memcpy(initial, gba->audio.psg.ch3.wavedata8, sizeof(initial));
            cupid_io_write(gba, REG_SOUND3CNT_L, (uint16_t)(0x80 | (bank << 6) | (dimension << 5)));
            cupid_io_write(gba, REG_SOUND3CNT_H, 0x2000);
            cupid_io_write(gba, REG_SOUND3CNT_X, 0x87FF);
            unsigned count = dimension ? 64 : 32;
            /* Begin at the queued first fetch; its startup delay is not a
             * hardware expectation in this pattern/order regression. */
            unsigned first = (unsigned)gba->audio.psg.ch3.nextUpdate;
            tick(gba, (int)first);
            for (unsigned step = 0; step < count; step++) {
                if (step) tick(gba, 8);
                /* A CPU wave read advances the addressed sound engine to now. */
                (void)cupid_io_read(gba, REG_WAVE_RAM0_LO);
                if (!dimension) CHECK(gba->audio.psg.ch3.sample == (int)((step * 3 + 4 + bank * 5) & 15));
            }
            CHECK(!memcmp(initial, gba->audio.psg.ch3.wavedata8, sizeof(initial)));
            destroy(gba);
        }
    }
}

static void mixer_deadlines_use_old_state_before_writes(void) {
    struct CupidGBA* gba = machine(0);
    cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
    cupid_io_write(gba, REG_SOUNDCNT_L, 0x2277);
    cupid_io_write(gba, REG_SOUNDCNT_H, 2);
    cupid_io_write(gba, REG_SOUND2CNT_L, 0xC0C0); /* 75% duty is high at step two. */
    cupid_io_write(gba, REG_SOUND2CNT_H, 0x87F0); /* 256 clocks per duty step. */
    tick(gba, 512);
    CHECK(gba->audio.sampleWritePos == 2 && gba->audio.sampleBuffer[0] == 9216);
    CHECK(gba->audio.sampleClocks[0] == 512);
    tick(gba, 88);
    cupid_memory_store8(&gba->cpu, GBA_BASE_IO + REG_SOUND2CNT_L, 0, NULL);
    CHECK(gba->audio.psg.ch2.index == 2 && gba->audio.psg.ch2.lastUpdate == 512);
    CHECK(gba->audio.sampleBuffer[0] == 9216);
    tick(gba, 424);
    CHECK(gba->audio.sampleWritePos == 4 && gba->audio.sampleBuffer[2] == 0);
    destroy(gba);
}

static void resolution_changes_retain_capture_history(void) {
    struct CupidGBA* gba = machine(0);
    tick(gba, 600);
    CHECK(gba->audio.sampleWritePos == 2 && gba->audio.lastSample == 512);
    cupid_io_write(gba, REG_SOUNDBIAS, 0xC200);
    CHECK(gba->audio.nextSample == 1024 && gba->audio.sampleClocks[0] == 512);
    tick(gba, 424);
    CHECK(gba->audio.sampleWritePos == 4 && gba->audio.sampleClocks[1] == 512);
    tick(gba, 192);
    CHECK(gba->audio.sampleWritePos == 10 && gba->audio.lastSample == 1216);
    for (unsigned i = 2; i < 5; i++) CHECK(gba->audio.sampleClocks[i] == 64);
    cupid_io_write(gba, REG_SOUNDBIAS, 0x0200);
    tick(gba, 64);
    CHECK(gba->audio.lastSample == 1280 && gba->audio.nextSample == 1536);
    tick(gba, 256);
    CHECK(gba->audio.sampleClocks[6] == 256 && gba->audio.lastSample == 1536);
    destroy(gba);
}

static void sampler_rates_and_late_dispatch_across_clock_wrap(void) {
    const uint64_t origins[] = {0, INT32_MAX - 4u, UINT32_MAX - 4u, UINT64_C(0x300000000) - 4};
    for (unsigned mode = 0; mode < 4; mode++) {
        for (unsigned origin = 0; origin < sizeof(origins) / sizeof(origins[0]); origin++) {
            struct CupidGBA* punctual = machine(origins[origin]);
            struct CupidGBA* late = machine(origins[origin]);
            cupid_io_write(punctual, REG_SOUNDBIAS, (uint16_t)(0x200 | (mode << 14)));
            cupid_io_write(late, REG_SOUNDBIAS, (uint16_t)(0x200 | (mode << 14)));
            for (unsigned clock = 0; clock < 2560; clock++) tick(punctual, 1);
            tick(late, 2560);
            unsigned frames = 1 + 2048 / (512 >> mode);
            CHECK(punctual->audio.sampleWritePos == (int)(frames * 2));
            CHECK(late->audio.lastSample == origins[origin] + 2560);
            CHECK(late->audio.sampleWritePos == punctual->audio.sampleWritePos);
            CHECK(!memcmp(late->audio.sampleBuffer, punctual->audio.sampleBuffer, sizeof(late->audio.sampleBuffer)));
            CHECK(!memcmp(late->audio.sampleClocks, punctual->audio.sampleClocks, sizeof(late->audio.sampleClocks)));
            destroy(punctual);
            destroy(late);
        }
    }
}

static void power_down_mutes_and_clears_modes_without_erasing_wave_ram(void) {
    struct CupidGBA* gba = machine(0);
    cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
    select_cpu_bank(gba, 1);
    cupid_memory_store32(&gba->cpu, GBA_BASE_IO + 0x09C, 0x76543210, NULL);
    cupid_io_write(gba, REG_SOUND3CNT_L, 0xE0);
    cupid_io_write(gba, REG_SOUND3CNT_H, 0xE034);
    cupid_io_write(gba, REG_SOUND3CNT_X, 0xC723);
    cupid_io_write(gba, REG_SOUND4CNT_L, 0xF123);
    cupid_io_write(gba, REG_SOUND4CNT_H, 0xC023);
    cupid_io_write(gba, REG_SOUNDCNT_H, 0x0304);
    cupid_memory_store32(&gba->cpu, GBA_BASE_IO + REG_FIFO_A_LO, 0x44332211, NULL);
    cupid_audio_timer_overflow(&gba->audio, 0, 0);
    tick(gba, 512);
    CHECK(gba->audio.sampleBuffer[0] != 0);
    uint8_t wave[32];
    memcpy(wave, gba->audio.psg.ch3.wavedata8, sizeof(wave));
    cupid_io_write(gba, REG_SOUNDCNT_X, 0);
    for (uint32_t addr = REG_SOUND1CNT_L; addr <= REG_SOUNDCNT_L; addr += 2) CHECK(cupid_io_read(gba, addr) == 0);
    CHECK(!memcmp(wave, gba->audio.psg.ch3.wavedata8, sizeof(wave)));
    tick(gba, 512);
    CHECK(gba->audio.sampleBuffer[2] == 0 && gba->audio.sampleBuffer[3] == 0);
    cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
    CHECK(cupid_io_read(gba, REG_SOUND3CNT_L) == 0 && cupid_io_read(gba, REG_SOUND4CNT_H) == 0);
    destroy(gba);
}

struct CaptureCount { unsigned count; uint64_t last; };

static void count_capture(void* context, uint64_t clock, int16_t left, int16_t right) {
    struct CaptureCount* capture = context;
    CHECK(clock == capture->last + 512 && left == 0 && right == 0);
    ++capture->count;
    capture->last = clock;
}

static void output_overrun_retains_valid_frames_and_complete_capture(void) {
    struct CupidGBA* gba = machine(0);
    struct CaptureCount capture = {0};
    gba->audio.sampleSink = count_capture;
    gba->audio.sampleSinkContext = &capture;
    tick(gba, (GBA_AUDIO_SAMPLES + 3) * 512);
    CHECK(capture.count == GBA_AUDIO_SAMPLES + 3);
    CHECK(gba->audio.sampleOverruns == 4);
    CHECK(gba->audio.sampleReadPos == 8 && gba->audio.sampleWritePos == 6);
    unsigned available = (gba->audio.sampleWritePos - gba->audio.sampleReadPos + GBA_AUDIO_SAMPLES * 2) % (GBA_AUDIO_SAMPLES * 2);
    CHECK(available == (GBA_AUDIO_SAMPLES - 1) * 2);
    CHECK(gba->audio.sampleClocks[gba->audio.sampleReadPos / 2] == 512);
    destroy(gba);
}

int main(void) {
    RUN(wave_banks_use_the_full_cpu_window_at_each_width);
    RUN(powered_off_mode_writes_leave_wave_ram_accessible);
    RUN(square_bytes_preserve_running_envelopes_and_lengths);
    RUN(wave_and_noise_bytes_do_not_repeat_other_fields);
    RUN(active_wave_fetches_preserve_the_other_bank);
    RUN(wave_nibble_order_and_complete_cycles_preserve_data);
    RUN(mixer_deadlines_use_old_state_before_writes);
    RUN(resolution_changes_retain_capture_history);
    RUN(sampler_rates_and_late_dispatch_across_clock_wrap);
    RUN(power_down_mutes_and_clears_modes_without_erasing_wave_ram);
    RUN(output_overrun_retains_valid_frames_and_complete_capture);
    return 0;
}
