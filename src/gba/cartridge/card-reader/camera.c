/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file camera.c
 * @brief Card-reader serial camera, acknowledged scanlines and cartridge registers.
 *
 * - Shift camera commands MSB first, with separate data and acknowledgement clocks.
 * - Preserve queued optical input across camera reset and free it on board removal.
 * - Bound every sensor read and schedule the selected LED/IRQ delay on console time.
 * - Retain exposure progress across PHI rate changes and clock gating.
 */
#include "cupid/gba.h"
#include "internal.h"

enum {
    SERIAL_IDLE,
    SERIAL_START,
    SERIAL_WRITE,
    SERIAL_WRITE_ACK,
    SERIAL_READ,
    SERIAL_READ_ACK,
    SERIAL_END
};

enum { CONTROL_LED = 8, CONTROL_SCAN = 16, CONTROL_PHI = 32, CONTROL_POWER = 64 };

static void cancel_irq(struct CupidCardReader *reader) {
    if (reader->irqEvent.context) {
        struct CupidGBA *gba = reader->irqEvent.context;
        cupid_timing_deschedule(&gba->timing, &reader->irqEvent);
    }
    reader->irqActive = false;
    reader->irqRemaining = reader->irqRate = 0;
}

void cupid_card_reader_reset(struct CupidCardReader *reader) {
    cancel_irq(reader);
    memset(reader->sensor, 0, sizeof(reader->sensor));
    memset(reader->camera, 0, sizeof(reader->camera));
    memset(reader->intensity, 0, sizeof(reader->intensity));
    reader->camera[0] = 0x12;
    /* Type 1 power-on camera dimensions precede the guest's 320x246 setup. */
    reader->camera[0x14] = 1;
    reader->camera[0x15] = 0x2E;
    reader->camera[0x16] = 1;
    reader->camera[0x17] = 0x92;
    reader->output = reader->led = 0;
    reader->reset = 4;
    reader->control[0] = 0;
    reader->control[1] = 0x80;
    reader->serialState = SERIAL_IDLE;
    reader->serialBits = reader->serialByte = reader->command = reader->index = 0;
    reader->scanX = 0;
    reader->scanY = 0;
}

void cupid_card_reader_init(struct CupidCardReader *reader) {
    memset(reader, 0, sizeof(*reader));
    cupid_card_reader_reset(reader);
}

void cupid_card_reader_deinit(struct CupidCardReader *reader) {
    cancel_irq(reader);
    free(reader->dots);
    for (unsigned i = 0; i < CUPID_CARD_QUEUE; i++)
        free(reader->queued[i]);
    memset(reader, 0, sizeof(*reader));
}

bool cupid_card_reader_queue(struct CupidCardReader *reader, const void *data, size_t size) {
    if (reader->queueCount == CUPID_CARD_QUEUE)
        return false;
    uint8_t *dots = malloc(CUPID_CARD_DOTS);
    if (!dots)
        return false;
    if (!cupid_card_decode(data, size, dots)) {
        free(dots);
        return false;
    }
    const unsigned slot = (reader->queueHead + reader->queueCount) % CUPID_CARD_QUEUE;
    reader->queued[slot] = dots;
    ++reader->queueCount;
    return true;
}

static void next_card(struct CupidCardReader *reader) {
    free(reader->dots);
    reader->dots = NULL;
    if (reader->queueCount) {
        reader->dots = reader->queued[reader->queueHead];
        reader->queued[reader->queueHead] = NULL;
        reader->queueHead = (reader->queueHead + 1) % CUPID_CARD_QUEUE;
        --reader->queueCount;
        reader->scanX = -24;
    }
}

static bool camera_register(unsigned index) {
    return index <= 2 || (index >= 0x10 && index <= 0x17) || (index >= 0x20 && index <= 0x23) ||
           (index >= 0x25 && index <= 0x28) || (index >= 0x30 && index <= 0x34) ||
           (index >= 0x50 && index <= 0x5A);
}

static uint8_t camera_read(const struct CupidCardReader *reader) {
    const unsigned index = reader->index & 127;
    return camera_register(index) ? reader->camera[index] : 0;
}

static void camera_write(struct CupidCardReader *reader, uint8_t value) {
    const unsigned index = reader->index & 127;
    if (!camera_register(index) || index == 0 || (index >= 0x57 && index <= 0x5A))
        return;
    reader->camera[index] = index >= 0x53 && index <= 0x55 ? value & 3 : value;
}

static void serial_byte(struct CupidCardReader *reader) {
    if (!reader->command)
        reader->command = reader->serialByte;
    else if (reader->command == 0x22) {
        reader->index = reader->serialByte;
        reader->command = 1;
    } else if (reader->command == 1) {
        camera_write(reader, reader->serialByte);
        ++reader->index;
    }
    reader->serialByte = reader->serialBits = 0;
}

static uint8_t serial_control(struct CupidCardReader *reader, uint8_t control) {
    const uint8_t old = reader->control[0];
    const bool high = (control & 2) != 0;
    if ((old & 2) && high && (control & 4)) {
        if ((old & 1) && !(control & 1)) {
            reader->serialState = SERIAL_START;
            reader->serialBits = reader->serialByte = reader->command = 0;
        } else if (!(old & 1) && (control & 1))
            reader->serialState = SERIAL_IDLE;
    }
    if ((old & 2) && !high) {
        switch (reader->serialState) {
            case SERIAL_START:
                reader->serialState = SERIAL_WRITE;
                break;
            case SERIAL_WRITE:
                if (control & 4) {
                    reader->serialByte = (uint8_t)((reader->serialByte << 1) | (control & 1));
                    if (++reader->serialBits == 8) {
                        serial_byte(reader);
                        reader->serialState = SERIAL_WRITE_ACK;
                    }
                }
                break;
            case SERIAL_WRITE_ACK:
                reader->serialState = reader->command == 0x23 ? SERIAL_READ : SERIAL_WRITE;
                break;
            case SERIAL_READ:
                if (!(control & 4) && ++reader->serialBits == 8) {
                    ++reader->index;
                    reader->serialBits = 0;
                    reader->serialState = SERIAL_READ_ACK;
                }
                break;
            case SERIAL_READ_ACK:
                if (control & 4)
                    reader->serialState = control & 1 ? SERIAL_END : SERIAL_READ;
                break;
            default:
                break;
        }
    }
    if (!(control & 4)) {
        control &= (uint8_t)~1;
        if (reader->serialState == SERIAL_READ)
            control |= (camera_read(reader) >> (7 - reader->serialBits)) & 1;
    }
    return control;
}

bool cupid_card_reader_read(struct CupidCardReader *reader, uint32_t address, uint16_t *value) {
    if ((address & 0xFFF80000) != 0x0DF80000)
        return false;
    switch ((address >> 17) & 3) {
        case 0:
            *value = reader->output;
            break;
        case 1:
            *value = reader->reset;
            break;
        case 2: {
            const unsigned index = (address & 254) / 2;
            *value = index < CUPID_CARD_SENSOR_WORDS ? reader->sensor[index] : 0;
            if (index >= 20)
                *value &= 127;
            break;
        }
        default:
            return false;
    }
    return true;
}

bool cupid_card_reader_write(struct CupidCardReader *reader, uint32_t address, uint16_t value) {
    if ((address & 0xFFF80000) != 0x0DF80000)
        return false;
    switch ((address >> 17) & 3) {
        case 0:
            reader->output = value & 15;
            break;
        case 1:
            if (value & 2)
                cupid_card_reader_reset(reader);
            reader->reset = (value & 0x10A) | 4;
            break;
        case 2:
            break;
        default:
            return false;
    }
    return true;
}

static bool port_address(uint32_t address) {
    return (address >> 24 == 14 || address >> 24 == 15) && (address & 0xFF80) == 0xFF80;
}

bool cupid_card_reader_read_port(struct CupidCardReader *reader, uint32_t address, uint8_t *value) {
    if (!port_address(address))
        return false;
    const unsigned port = address & 63;
    if (port < 48)
        *value = reader->intensity[port];
    else if (port < 50)
        *value = reader->control[port - 48];
    else if (port < 52)
        *value = (uint8_t)(reader->led >> ((port - 50) * 8));
    else
        *value = 0;
    return true;
}

static unsigned clock_rate(const struct CupidGBA *gba) {
    const unsigned phi = (gba->memory.io[REG_WAITCNT >> 1] >> 11) & 3;
    const unsigned control = gba->cartridge.cardReader.control[0];
    /* Four exposure units per PHI period retain fractional progress when
     * switching between the console's /1, /2 and /4 clock outputs. */
    return phi && (control & (CONTROL_PHI | CONTROL_POWER)) == (CONTROL_PHI | CONTROL_POWER)
               ? 1u << (phi - 1)
               : 0;
}

static bool scanning(const struct CupidGBA *gba) {
    const struct CupidCardReader *reader = &gba->cartridge.cardReader;
    return (reader->control[0] & (CONTROL_SCAN | CONTROL_PHI | CONTROL_POWER)) ==
               (CONTROL_SCAN | CONTROL_PHI | CONTROL_POWER) &&
           clock_rate(gba);
}

static void reschedule_irq(struct CupidGBA *gba) {
    struct CupidCardReader *reader = &gba->cartridge.cardReader;
    if (!reader->irqActive)
        return;
    const uint64_t now = cupid_timing_global_time(&gba->timing);
    const uint64_t elapsed = now - reader->irqClock;
    if (reader->irqRate) {
        if (elapsed >= (reader->irqRemaining + reader->irqRate - 1) / reader->irqRate)
            reader->irqRemaining = 0;
        else
            reader->irqRemaining -= (uint32_t)elapsed * reader->irqRate;
    }
    reader->irqClock = now;
    reader->irqRate = clock_rate(gba);
    cupid_timing_deschedule(&gba->timing, &reader->irqEvent);
    if (reader->irqRate)
        cupid_timing_schedule(
            &gba->timing, &reader->irqEvent,
            (int32_t)((reader->irqRemaining + reader->irqRate - 1) / reader->irqRate));
}

static void scan_irq(struct CupidTiming *timing, void *context, uint32_t late) {
    (void)late;
    struct CupidGBA *gba = context;
    struct CupidCardReader *reader = &gba->cartridge.cardReader;
    reader->irqActive = false;
    reader->irqRemaining = reader->irqRate = 0;
    if (scanning(gba) && (reader->control[0] & CONTROL_LED))
        cupid_raise_irq_at(gba, IRQ_GAMEPAK, cupid_timing_global_time(timing));
}

static void begin_image(struct CupidCardReader *reader) {
    memset(reader->sensor + 20, 0, 48 * sizeof(uint16_t));
    reader->sensor[68] = 127;
}

static void read_line(struct CupidGBA *gba) {
    struct CupidCardReader *reader = &gba->cartridge.cardReader;
    if (!reader->dots)
        next_card(reader);
    memset(reader->sensor, 0, 40);
    const unsigned blockRow = reader->scanY / 41 < 6 ? reader->scanY / 41 : 5;
    for (unsigned pixel = 0; pixel < 320; pixel++) {
        bool ink = false;
        if (reader->dots && reader->scanY >= 10 && reader->scanY < 130) {
            const unsigned row = (reader->scanY - 10) / 3;
            const int column = 16 + (reader->scanX + (int)pixel) / 3;
            if (column >= 0 && column < CUPID_CARD_STRIDE &&
                reader->dots[row * CUPID_CARD_STRIDE + (unsigned)column])
                ink = true;
        }
        /* Normalized strips supply monochrome endpoints before the PGA's
         * per-block threshold. Statistics retain those pre-threshold levels. */
        const unsigned brightness = ink ? 0 : 127;
        const unsigned block = blockRow * 8 + 7 - pixel / 40;
        if (reader->sensor[20 + block] < brightness)
            reader->sensor[20 + block] = (uint16_t)brightness;
        if (reader->sensor[68] > brightness)
            reader->sensor[68] = (uint16_t)brightness;
        if (brightness <= reader->intensity[block]) {
            const unsigned bit = pixel % 16;
            reader->sensor[19 - pixel / 16] |= (uint16_t)(1u << (bit < 8 ? bit + 8 : bit - 8));
        }
    }
    reader->control[1] |= 2;
    if (reader->control[0] & CONTROL_LED) {
        if (!reader->irqEvent.context)
            reader->irqEvent = (struct CupidTimingEvent){.context = gba,
                                                         .callback = scan_irq,
                                                         .name = "Card reader line interrupt",
                                                         .priority = 0x40};
        if (!reader->irqActive) {
            const unsigned duration = reader->led < 0x2000 ? reader->led : 0x2000;
            /* The selected LED counter divides PHI by two: eight quarter-PHI
             * units per count, or two console clocks at the nominal rate. */
            reader->irqRemaining = duration * 8;
            reader->irqClock = cupid_timing_global_time(&gba->timing);
            reader->irqRate = 0;
            reader->irqActive = true;
            reschedule_irq(gba);
        }
    }
}

void cupid_card_reader_update_clock(struct CupidGBA *gba) {
    reschedule_irq(gba);
    struct CupidCardReader *reader = &gba->cartridge.cardReader;
    if (scanning(gba) && (reader->control[0] & CONTROL_LED) && !(reader->control[1] & 2))
        read_line(gba);
}

bool cupid_card_reader_write_port(struct CupidGBA *gba, uint32_t address, uint8_t value) {
    if (!port_address(address))
        return false;
    struct CupidCardReader *reader = &gba->cartridge.cardReader;
    if ((address & 63) < 48) {
        reader->intensity[address & 63] = value & 127;
        return true;
    }
    switch (address & 63) {
        case 48: {
            const uint8_t old = reader->control[0];
            if (value & CONTROL_POWER)
                reader->control[0] = serial_control(reader, value & 127);
            else {
                /* An unpowered camera cannot consume a serial command.
                 * Keep the host's output latch without inventing input bias. */
                reader->control[0] = value & 127;
                reader->serialState = SERIAL_IDLE;
                reader->serialBits = reader->serialByte = reader->command = 0;
            }
            if (!(old & CONTROL_SCAN) && (value & CONTROL_SCAN)) {
                if (reader->scanX > 0)
                    next_card(reader);
                reader->scanX = 0;
                reader->scanY = 0;
                begin_image(reader);
            }
            if ((reader->control[0] & (CONTROL_POWER | CONTROL_SCAN | CONTROL_LED)) !=
                (CONTROL_POWER | CONTROL_SCAN | CONTROL_LED))
                cancel_irq(reader);
            cupid_card_reader_update_clock(gba);
            break;
        }
        case 49:
            reader->control[1] = 0x80 | (value & 0x30) | (reader->control[1] & value & 2);
            if (scanning(gba) && !(reader->control[1] & 2)) {
                const unsigned height =
                    ((unsigned)reader->camera[0x14] << 8) | reader->camera[0x15];
                if (++reader->scanY == height) {
                    reader->scanY = 0;
                    begin_image(reader);
                    if (reader->scanX < 4050)
                        reader->scanX += 210;
                }
                read_line(gba);
            }
            break;
        case 50:
            reader->led = (reader->led & 0xFF00) | value;
            break;
        case 51:
            reader->led = (reader->led & 0x00FF) | (uint16_t)value << 8;
            break;
        default:
            break;
    }
    return true;
}

void cupid_card_reader_calibrate(struct CupidGBA *gba) {
    struct CupidCardReader *reader = &gba->cartridge.cardReader;
    struct CupidSavedata *save = &gba->memory.savedata;
    if (reader->calibrationChecked || !save->data || save->size < 0xF000 ||
        (save->type != SAVE_FLASH512 && save->type != SAVE_FLASH1M))
        return;
    reader->calibrationChecked = true;
    /* A new, erased battery gets deterministic camera defaults. Never repair
     * a partly populated sector, or undo a later guest erase/program command. */
    for (unsigned offset = 0xD000; offset <= 0xE000; offset += 0x1000) {
        uint8_t *sector = save->data + offset;
        bool erased = true;
        for (unsigned byte = 0; byte < 0x1000; byte++)
            if (sector[byte] != 0xFF) {
                erased = false;
                break;
            }
        if (!erased)
            continue;
        memset(sector, 0, 0x1000);
        memcpy(sector, "Card-E Reader 2001", 18);
        memset(sector + 0x16, 0x30, 48);
        sector[0x46] = 0x18;
        sector[0x47] = 0x2D;
        sector[0x48] = 0x8F;
        sector[0x49] = 3;
        sector[0x4C] = 0xC0;
        sector[0x4D] = 0xFD;
        sector[0x4E] = 0x77;
        sector[0x52] = 1;
        unsigned sum = 0;
        for (unsigned byte = 0; byte < 0x1000; byte += 2)
            sum += sector[byte] | (unsigned)sector[byte + 1] << 8;
        const uint16_t checksum = (uint16_t)~(sum + (sum >> 16));
        sector[0x14] = (uint8_t)checksum;
        sector[0x15] = (uint8_t)(checksum >> 8);
        save->dirty = true;
    }
}
