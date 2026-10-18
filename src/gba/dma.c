/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"

enum DMAPhase { DMA_IDLE, DMA_HEAD, DMA_READ, DMA_READ_HIGH, DMA_WRITE, DMA_WRITE_HIGH, DMA_TAIL };

static void phase_callback(struct CupidTiming* timing, void* context, uint32_t late);

static uint32_t transfer_count(const struct CupidDMA* dma) {
    uint32_t count = (uint32_t)dma->count & (dma->id == 3 ? 0xFFFF : 0x3FFF);
    return count ? count : (dma->id == 3 ? 0x10000 : 0x4000);
}

static int next_channel(const struct CupidMemory* memory) {
    _Static_assert(GBA_DMA_COUNT == 4, "DMA arbitration requires four channels");
    /* Hardware priority is fixed. Read the live channel flags directly so
     * arbitration needs neither an idle-channel loop nor cached readiness. */
    if (memory->dma[0].enable && memory->dma[0].ready) return 0;
    if (memory->dma[1].enable && memory->dma[1].ready) return 1;
    if (memory->dma[2].enable && memory->dma[2].ready) return 2;
    if (memory->dma[3].enable && memory->dma[3].ready) return 3;
    return -1;
}

static void phase_at(struct CupidGBA* gba, enum DMAPhase phase, uint64_t when) {
    gba->memory.dmaPhase = phase;
    cupid_timing_schedule_absolute(&gba->timing, &gba->memory.dmaEvent,
                                   (int32_t)(uint32_t)when);
}

static unsigned access_cycles(struct CupidMemory* memory, struct CupidDMA* dma,
                              uint32_t address, bool read) {
    dma->accessAddress = address;
    dma->gpioHalfCycles = 0;
    unsigned penalty = cupid_prefetch_before_data(dma->gba, address);
    if (read && address < GBA_BASE_EWRAM) return 1 + penalty;
    unsigned region = address >> GBA_BASE_OFFSET;
    bool sequential = true;
    if (address >= GBA_BASE_ROM0 && !dma->externalSeen) {
        sequential = false;
        dma->externalSeen = true;
    }
    if (region >= GBA_REGION_ROM0 && region <= GBA_REGION_ROM2_EX) {
        /* The programmed address predicts the next access's page restart.
         * Fixed and decrementing pointers retain that delayed boundary edge.
         * Nonsequential strobes latch an address; sequential reads and writes
         * advance the shared counter, including source/destination crossovers. */
        if (memory->dmaROMBoundary) sequential = false;
        memory->dmaROMBoundary = !((address + (dma->transferWord ? 4 : 2)) & 0x1FFFF);
        if (!sequential) memory->dmaROMAddress = address & 0x01FFFFFF;
        bool eeprom = cupid_memory_is_eeprom_address(memory, address) &&
            (memory->savedata.type == SAVE_EEPROM || memory->savedata.type == SAVE_EEPROM512);
        if (!eeprom) dma->accessAddress = (address & 0xFE000000) | memory->dmaROMAddress;
        memory->dmaROMAddress = (memory->dmaROMAddress + (dma->transferWord ? 4 : 2)) & 0x01FFFFFF;
    }
    bool word = dma->transferWord && region != GBA_REGION_SRAM &&
                region != GBA_REGION_SRAM_MIRROR;
    unsigned cycles = 1 + penalty + (unsigned)(word
        ? (sequential ? memory->waitstatesSeq32[region] : memory->waitstatesNonseq32[region])
        : (sequential ? memory->waitstatesSeq16[region] : memory->waitstatesNonseq16[region]));
    if (word && cupid_cartridge_gpio_address(dma->gba, dma->accessAddress)) {
        dma->gpioHalfCycles = (unsigned)memory->waitstatesSeq16[region] + 1;
        cycles -= dma->gpioHalfCycles;
    }
    return cycles;
}

static void read_next(struct CupidGBA* gba, uint64_t when) {
    struct CupidMemory* memory = &gba->memory;
    int ch = next_channel(memory);
    if (ch < 0) {
        memory->activeDMA = -1;
        phase_at(gba, DMA_TAIL, when + 1);
        return;
    }
    if (ch != memory->activeDMA) memory->dma[ch].externalSeen = false;
    memory->activeDMA = ch;
    struct CupidDMA* dma = &memory->dma[ch];
    dma->transferWord = dma->wordTransfer || dma->fifo;
    int width = dma->transferWord ? 4 : 2;
    static const int offsets[] = {1, -1, 0, 0};
    /* The DMA pointer keeps its programmed step. Sequential ROM strobes
     * use the separate cartridge counter in access_cycles(). */
    dma->sourceOffset = offsets[dma->srcControl] * width;
    dma->destOffset = dma->fifo ? 0 : (dma->destControl == 3 ? width
        : offsets[dma->destControl] * width);
    dma->nextSource &= ~(uint32_t)(width - 1);
    dma->nextDest &= ~(uint32_t)(width - 1);
    phase_at(gba, DMA_READ, when + access_cycles(memory, dma, dma->nextSource, true));
}

static void begin_transfer(struct CupidGBA* gba, uint64_t timestamp) {
    struct CupidMemory* memory = &gba->memory;
    memory->dmaRunning = true;
    memory->activeDMA = -1;
    memory->fetchNonseq = true;
    gba->cpuBlocked = true;
    gba->dmaPC = gba->cpu.gprs[ARM_PC];
    phase_at(gba, DMA_HEAD, timestamp + 1);
}

void cupid_dma_kick(struct CupidGBA* gba, uint64_t timestamp) {
    const struct CupidMemory* memory = &gba->memory;
    if (memory->dmaRunning || memory->busBusy || memory->busLocked ||
        next_channel(memory) < 0) return;
    begin_transfer(gba, timestamp);
}

void cupid_dma_poll(struct CupidGBA* gba) {
    const struct CupidMemory* memory = &gba->memory;
    if (memory->dmaRunning || memory->busBusy || memory->busLocked ||
        next_channel(memory) < 0) return;
    /* The clock getter has no side effects. Idle CPU strobes need neither a
     * timestamp nor a queued event; accepted transfers keep the same clock. */
    begin_transfer(gba, cupid_timing_global_time(&gba->timing));
}

static void activation_callback(struct CupidTiming* timing, void* context, uint32_t late) {
    struct CupidDMA* dma = context;
    (void)late;
    if (!dma->enable) return;
    dma->ready = true;
    dma->externalSeen = false;
    cupid_dma_kick(dma->gba, cupid_timing_global_time(timing));
}

static void request_at(struct CupidGBA* gba, int ch, uint64_t timestamp) {
    struct CupidDMA* dma = &gba->memory.dma[ch];
    if (!dma->enable || dma->ready ||
        cupid_timing_is_scheduled(&gba->timing, &dma->requestEvent)) return;
    cupid_timing_schedule_absolute(&gba->timing, &dma->requestEvent,
                                   (int32_t)(uint32_t)(timestamp + 2));
}

static void complete_channel(struct CupidGBA* gba, struct CupidDMA* dma, uint64_t when) {
    dma->ready = false;
    dma->busyUntil = when;
    if (dma->doIrq) cupid_raise_irq_at(gba, IRQ_DMA0 << dma->id, when);
    if (dma->repeat && dma->timing != DMA_TIMING_NOW) {
        dma->nextCount = dma->fifo ? 4 : (int32_t)transfer_count(dma);
        if (dma->destControl == GBA_DMA_INCREMENT_RELOAD && !dma->fifo) {
            dma->nextDest = dma->dest & (dma->transferWord ? ~3u : ~1u);
        }
    } else {
        dma->enable = false;
        dma->reg &= 0x7FFF;
        gba->memory.io[(REG_DMA0CNT_HI + dma->id * 12) >> 1] = dma->reg;
    }
}

static void read_value(struct CupidGBA* gba, struct CupidDMA* dma) {
    if (dma->nextSource < GBA_BASE_EWRAM) return;
    gba->performingDMA = 1 | (dma->id << 1);
    uint32_t value;
    if (gba->memory.dmaPhase == DMA_READ_HIGH) {
        value = dma->gpioReadLow | (uint32_t)cupid_memory_loadU16(&gba->cpu, dma->accessAddress + 2, NULL) << 16;
    } else if (dma->transferWord) {
        value = cupid_memory_load32(&gba->cpu, dma->accessAddress, NULL);
    } else {
        value = cupid_memory_loadU16(&gba->cpu, dma->accessAddress, NULL);
        value |= value << 16;
    }
    dma->latch = value;
    gba->memory.dmaBus = value;
    gba->memory.lastWasDMA = true;
    gba->performingDMA = 0;
}

static void write_value(struct CupidGBA* gba, struct CupidDMA* dma) {
    uint32_t value = dma->latch;
    gba->performingDMA = 1 | (dma->id << 1);
    if (gba->memory.dmaPhase == DMA_WRITE_HIGH) {
        cupid_memory_store16(&gba->cpu, dma->accessAddress + 2, (int16_t)(value >> 16), NULL);
    } else if (dma->transferWord) {
        cupid_memory_store32(&gba->cpu, dma->accessAddress, (int32_t)value, NULL);
    } else {
        value >>= (dma->nextDest & 2) * 8;
        if (cupid_memory_is_eeprom_address(&gba->memory, dma->nextDest)) {
            struct CupidSavedata* save = &gba->memory.savedata;
            if (save->type == SAVE_AUTODETECT) {
                save->type = SAVE_EEPROM512;
                cupid_savedata_init_eeprom(save);
            }
            if (save->type == SAVE_EEPROM || save->type == SAVE_EEPROM512) {
                cupid_savedata_write_eeprom(save, (uint16_t)value, (uint32_t)dma->nextCount);
            } else {
                cupid_memory_store16(&gba->cpu, dma->accessAddress, (int16_t)value, NULL);
            }
        } else {
            cupid_memory_store16(&gba->cpu, dma->accessAddress, (int16_t)value, NULL);
        }
    }
    gba->memory.lastWasDMA = true;
    gba->performingDMA = 0;
}

static void phase_callback(struct CupidTiming* timing, void* context, uint32_t late) {
    struct CupidGBA* gba = context;
    struct CupidMemory* memory = &gba->memory;
    uint64_t when = cupid_timing_global_time(timing);
    (void)late;
    if (memory->dmaPhase == DMA_HEAD) {
        read_next(gba, when);
    } else if (memory->dmaPhase == DMA_TAIL) {
        memory->dmaRunning = false;
        memory->dmaPhase = DMA_IDLE;
        memory->activeDMA = -1;
        gba->cpuBlocked = false;
        cupid_dma_kick(gba, when);
    } else {
        struct CupidDMA* dma = &memory->dma[memory->activeDMA];
        if (!dma->enable) {
            read_next(gba, when);
        } else if (memory->dmaPhase == DMA_READ || memory->dmaPhase == DMA_READ_HIGH) {
            if (memory->dmaPhase == DMA_READ && dma->gpioHalfCycles) {
                gba->performingDMA = 1 | (dma->id << 1);
                dma->gpioReadLow = (uint16_t)cupid_memory_loadU16(&gba->cpu, dma->accessAddress, NULL);
                gba->performingDMA = 0;
                phase_at(gba, DMA_READ_HIGH, when + dma->gpioHalfCycles);
                return;
            }
            cupid_prefetch_after_data(gba);
            read_value(gba, dma);
            phase_at(gba, DMA_WRITE, when + access_cycles(memory, dma, dma->nextDest, false));
        } else if (memory->dmaPhase == DMA_WRITE || memory->dmaPhase == DMA_WRITE_HIGH) {
            if (memory->dmaPhase == DMA_WRITE && dma->gpioHalfCycles) {
                gba->performingDMA = 1 | (dma->id << 1);
                cupid_memory_store16(&gba->cpu, dma->accessAddress, (int16_t)dma->latch, NULL);
                gba->performingDMA = 0;
                phase_at(gba, DMA_WRITE_HIGH, when + dma->gpioHalfCycles);
                return;
            }
            cupid_prefetch_after_data(gba);
            write_value(gba, dma);
            dma->nextSource += (uint32_t)dma->sourceOffset;
            dma->nextDest += (uint32_t)dma->destOffset;
            if (dma->enable && --dma->nextCount == 0) complete_channel(gba, dma, when);
            read_next(gba, when);
        }
    }
}

void cupid_dma_init(struct CupidGBA* gba) {
    gba->memory.dmaEvent = (struct CupidTimingEvent){.context = gba,
        .callback = phase_callback, .name = "DMA bus phase", .priority = 0};
    for (unsigned ch = 0; ch < GBA_DMA_COUNT; ch++) {
        struct CupidDMA* dma = &gba->memory.dma[ch];
        memset(dma, 0, sizeof(*dma));
        dma->gba = gba;
        dma->id = ch;
        dma->requestEvent = (struct CupidTimingEvent){.context = dma,
            .callback = activation_callback, .name = "DMA activation", .priority = 0};
    }
}

void cupid_dma_reset(struct CupidGBA* gba) {
    cupid_timing_deschedule(&gba->timing, &gba->memory.dmaEvent);
    for (unsigned ch = 0; ch < GBA_DMA_COUNT; ch++) {
        cupid_timing_deschedule(&gba->timing, &gba->memory.dma[ch].requestEvent);
    }
    cupid_dma_init(gba);
    gba->memory.activeDMA = -1;
    gba->memory.dmaPhase = DMA_IDLE;
    gba->memory.dmaRunning = false;
    gba->memory.dmaBus = 0;
    gba->memory.dmaROMAddress = 0;
    gba->memory.dmaROMBoundary = false;
    gba->memory.lastWasDMA = false;
    gba->cpuBlocked = false;
}

void cupid_dma_write_sad(struct CupidGBA* gba, int ch, uint32_t address) {
    gba->memory.dma[ch].source = address & (ch == 0 ? 0x07FFFFFF : 0x0FFFFFFF);
}

void cupid_dma_write_dad(struct CupidGBA* gba, int ch, uint32_t address) {
    gba->memory.dma[ch].dest = address & (ch == 3 ? 0x0FFFFFFF : 0x07FFFFFF);
}

void cupid_dma_write_cnt_lo(struct CupidGBA* gba, int ch, uint16_t count) {
    gba->memory.dma[ch].count = count & (ch == 3 ? 0xFFFF : 0x3FFF);
}

void cupid_dma_write_cnt(struct CupidGBA* gba, int ch, uint16_t value) {
    struct CupidDMA* dma = &gba->memory.dma[ch];
    bool wasEnabled = dma->enable;
    value &= ch == 3 ? 0xFFE0 : 0xF7E0;
    dma->reg = value;
    gba->memory.io[(REG_DMA0CNT_HI + ch * 12) >> 1] = value;
    dma->destControl = DMA_REG_DEST_CONTROL(value);
    dma->srcControl = DMA_REG_SRC_CONTROL(value);
    dma->repeat = DMA_REG_REPEAT(value);
    dma->wordTransfer = DMA_REG_WIDTH(value);
    dma->timing = DMA_REG_TIMING(value);
    dma->doIrq = DMA_REG_IRQ(value);
    dma->enable = DMA_REG_ENABLE(value);
    if (!dma->enable) {
        cupid_timing_deschedule(&gba->timing, &dma->requestEvent);
        dma->ready = false;
        dma->nextCount = 0;
    } else if (!wasEnabled) {
        dma->fifo = ch >= 1 && ch <= 2 && dma->timing == DMA_TIMING_CUSTOM;
        dma->transferWord = dma->wordTransfer || dma->fifo;
        unsigned width = dma->transferWord ? 4 : 2;
        dma->nextSource = dma->source & ~(width - 1);
        dma->nextDest = dma->dest & ~(width - 1);
        dma->nextCount = dma->fifo ? 4 : (int32_t)transfer_count(dma);
        static const int offsets[] = {1, -1, 0, 0};
        dma->sourceOffset = offsets[dma->srcControl] * (int)width;
        dma->destOffset = dma->fifo ? 0 : (dma->destControl == 3 ? (int)width
            : offsets[dma->destControl] * (int)width);
        if (dma->timing == DMA_TIMING_NOW) cupid_dma_transfer(gba, ch);
        if (dma->fifo) cupid_audio_schedule_fifo_dma(&gba->audio, ch, dma);
    }
}

void cupid_dma_transfer(struct CupidGBA* gba, int ch) {
    request_at(gba, ch, cupid_timing_global_time(&gba->timing));
}

void cupid_dma_request_fifo(struct CupidGBA* gba, int ch, uint64_t timestamp) {
    if (ch >= 1 && ch <= 2 && gba->memory.dma[ch].timing == DMA_TIMING_CUSTOM) {
        request_at(gba, ch, timestamp);
    }
}

static void request_timing(struct CupidGBA* gba, enum CupidDMATiming timing) {
    for (int ch = 0; ch < GBA_DMA_COUNT; ch++) {
        if (gba->memory.dma[ch].timing == (int)timing) cupid_dma_transfer(gba, ch);
    }
}

void cupid_dma_run_hblank(struct CupidGBA* gba, int32_t cycles) {
    (void)cycles;
    request_timing(gba, GBA_DMA_TIMING_HBLANK);
}

void cupid_dma_run_vblank(struct CupidGBA* gba, int32_t cycles) {
    (void)cycles;
    request_timing(gba, GBA_DMA_TIMING_VBLANK);
}

void cupid_dma_run_display(struct CupidGBA* gba) {
    struct CupidDMA* dma = &gba->memory.dma[3];
    if (!dma->enable || dma->timing != DMA_TIMING_CUSTOM) return;
    if (gba->video.vcount >= 2 && gba->video.vcount < 162) cupid_dma_transfer(gba, 3);
    else if (gba->video.vcount == 162) cupid_dma_write_cnt(gba, 3, dma->reg & 0x7FFF);
}
