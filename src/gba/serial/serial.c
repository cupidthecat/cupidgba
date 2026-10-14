#include "cupid/gba.h"

enum { CONTROL_START = 0x80, CONTROL_IRQ = 0x4000 };
/* Hardware-source timer observations include six clocks beyond the nominal
 * bit periods. Model status/data publication as a separate completion stage;
 * the electrical placement of this interval still needs pin measurements. */
enum { COMPLETION_CLOCKS = 6 };

enum CupidSerialMode cupid_serial_mode(const struct CupidSerial* serial) {
    if (serial->rcnt & 0x8000) {
        return (serial->rcnt & 0x4000) ? SERIAL_JOY : SERIAL_GPIO;
    }
    return (enum CupidSerialMode)((serial->control >> 12) & 3);
}

uint8_t cupid_serial_pins(const struct CupidSerial* serial) {
    uint8_t inputs = (serial->inputLevels & serial->inputMask) | (15 & ~serial->inputMask);
    return (inputs & ~serial->outputMask) | (serial->outputLevels & serial->outputMask);
}

static void update_outputs(struct CupidSerial* serial) {
    uint8_t mask = 0, levels = 0;
    enum CupidSerialMode mode = cupid_serial_mode(serial);
    if (mode == SERIAL_GPIO) {
        mask = (serial->rcnt >> 4) & 15;
        levels = serial->rcnt & mask;
    } else if (mode == SERIAL_NORMAL8 || mode == SERIAL_NORMAL32) {
        bool owner = serial->active ? serial->internalClock : (serial->control & 1) != 0;
        mask = SERIAL_SO | (owner ? SERIAL_SC : SERIAL_SD);
        if (owner && (!serial->active || serial->clockHigh)) levels |= SERIAL_SC;
        if (serial->active ? serial->outputHigh : (serial->control & 8) != 0) levels |= SERIAL_SO;
    } else if (mode == SERIAL_JOY) {
        /* Idle clock terminals; no JOY bus transaction engine is attached. */
        mask = SERIAL_SC | SERIAL_SD;
    }
    if (mask == serial->outputMask && levels == serial->outputLevels) return;
    serial->outputMask = mask;
    serial->outputLevels = levels;
    if (serial->outputChanged) {
        serial->outputChanged(serial->outputContext, mask, levels,
                             cupid_timing_global_time(&serial->gba->timing));
    }
}

static void cancel_transfer(struct CupidSerial* serial) {
    cupid_timing_deschedule(&serial->gba->timing, &serial->edgeEvent);
    serial->active = false;
    serial->remaining = 0;
    serial->clockHigh = true;
}

static void finish_transfer(struct CupidSerial* serial) {
    struct CupidGBA* gba = serial->gba;
    if (serial->length32) {
        gba->memory.io[REG_SIODATA32_LO >> 1] = (uint16_t)serial->receive;
        gba->memory.io[REG_SIODATA32_HI >> 1] = (uint16_t)(serial->receive >> 16);
    } else {
        uint16_t* data = &gba->memory.io[REG_SIOMLT_SEND >> 1];
        *data = (*data & 0xFF00) | (serial->receive & 0xFF);
    }
    serial->active = false;
    serial->control &= ~CONTROL_START;
    gba->memory.io[REG_SIOCNT >> 1] = serial->control;
    if (serial->control & CONTROL_IRQ) {
        cupid_raise_irq_at(gba, IRQ_SIO, cupid_timing_global_time(&gba->timing));
    }
    update_outputs(serial);
}

static void clock_edge(struct CupidSerial* serial, bool high) {
    serial->clockHigh = high;
    if (!high) {
        serial->outputHigh = (serial->transmit & (serial->length32 ? 0x80000000u : 0x80u)) != 0;
        serial->transmit <<= 1;
        update_outputs(serial);
        return;
    }
    serial->receive = (serial->receive << 1) | ((cupid_serial_pins(serial) & SERIAL_SI) != 0);
    --serial->remaining;
    update_outputs(serial);
    if (!serial->remaining) {
        cupid_timing_schedule(&serial->gba->timing, &serial->edgeEvent, COMPLETION_CLOCKS);
    }
}

static void edge_callback(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct CupidSerial* serial = context;
    if (!serial->active) return;
    if (!serial->remaining) {
        finish_transfer(serial);
        return;
    }
    if (!serial->internalClock) return;
    clock_edge(serial, !serial->clockHigh);
    if (serial->active && serial->remaining) {
        /* Relative scheduling inside the callback uses its original deadline,
         * so late dispatch preserves every edge and IRQ timestamp. */
        cupid_timing_schedule(timing, &serial->edgeEvent, (int32_t)serial->halfPeriod);
    }
}

static void start_transfer(struct CupidSerial* serial) {
    struct CupidGBA* gba = serial->gba;
    serial->length32 = cupid_serial_mode(serial) == SERIAL_NORMAL32;
    serial->transmit = serial->length32 ?
        gba->memory.io[REG_SIODATA32_LO >> 1] | ((uint32_t)gba->memory.io[REG_SIODATA32_HI >> 1] << 16) :
        gba->memory.io[REG_SIOMLT_SEND >> 1] & 0xFF;
    serial->receive = 0;
    serial->remaining = serial->length32 ? 32 : 8;
    serial->internalClock = (serial->control & 1) != 0;
    serial->halfPeriod = (serial->control & 2) ? 4 : 32;
    serial->clockHigh = true;
    serial->outputHigh = (serial->control & 8) != 0;
    serial->active = true;
    update_outputs(serial);
    if (serial->internalClock) {
        cupid_timing_schedule(&gba->timing, &serial->edgeEvent, (int32_t)serial->halfPeriod);
    }
}

void cupid_serial_init(struct CupidSerial* serial, struct CupidGBA* gba) {
    memset(serial, 0, sizeof(*serial));
    serial->gba = gba;
    serial->edgeEvent = (struct CupidTimingEvent){.context = serial,
        .callback = edge_callback, .name = "Serial clock edge", .priority = 0x80};
    cupid_serial_reset(serial);
}

void cupid_serial_reset(struct CupidSerial* serial) {
    cancel_transfer(serial);
    serial->control = 0;
    serial->rcnt = 0x8000;
    serial->transmit = serial->receive = 0;
    memset(&serial->gba->memory.io[REG_SIODATA32_LO >> 1], 0, 12);
    serial->gba->memory.io[REG_JOYCNT >> 1] = 0;
    memset(&serial->gba->memory.io[REG_JOY_RECV_LO >> 1], 0, 10);
    serial->gba->memory.io[REG_SIOCNT >> 1] = 0;
    serial->gba->memory.io[REG_RCNT >> 1] = serial->rcnt;
    update_outputs(serial);
}

void cupid_serial_set_output_callback(struct CupidSerial* serial,
    void (*callback)(void*, uint8_t, uint8_t, uint64_t), void* context) {
    serial->outputChanged = callback;
    serial->outputContext = context;
    if (callback) callback(context, serial->outputMask, serial->outputLevels,
                           cupid_timing_global_time(&serial->gba->timing));
}

void cupid_serial_set_input(struct CupidSerial* serial, uint8_t mask, uint8_t levels) {
    uint8_t before = cupid_serial_pins(serial);
    serial->inputMask = mask & 15;
    serial->inputLevels = levels & mask & 15;
    uint8_t after = cupid_serial_pins(serial);
    if (cupid_serial_mode(serial) == SERIAL_GPIO && (serial->rcnt & 0x100) &&
        !(serial->outputMask & SERIAL_SI) && (before & SERIAL_SI) && !(after & SERIAL_SI)) {
        cupid_raise_irq_at(serial->gba, IRQ_SIO, cupid_timing_global_time(&serial->gba->timing));
    }
    if (!serial->gba->stopped && serial->active && serial->remaining &&
        !serial->internalClock && ((before ^ after) & SERIAL_SC)) {
        clock_edge(serial, (after & SERIAL_SC) != 0);
    }
}

uint16_t cupid_serial_read(struct CupidSerial* serial, uint32_t address) {
    struct CupidGBA* gba = serial->gba;
    enum CupidSerialMode mode = cupid_serial_mode(serial);
    switch (address) {
    case REG_SIOCNT:
        switch ((serial->control >> 12) & 3) {
        case SERIAL_MULTI: return (serial->control & 0x7F83) | 0xC;
        case SERIAL_UART: return (serial->control & 0x7F8F) | 0x20;
        default: return (serial->control & 0x7F8B) | (cupid_serial_pins(serial) & SERIAL_SI);
        }
    case REG_RCNT: return (serial->rcnt & 0xC1F0) | cupid_serial_pins(serial);
    case REG_SIODATA32_LO: case REG_SIODATA32_HI:
        return (mode == SERIAL_NORMAL32 || mode == SERIAL_MULTI) ? gba->memory.io[address >> 1] : 0;
    case REG_SIOMULTI2: case REG_SIOMULTI3:
        return mode == SERIAL_MULTI ? gba->memory.io[address >> 1] : 0;
    case REG_SIOMLT_SEND:
        return mode == SERIAL_UART ? 0 : gba->memory.io[address >> 1];
    case REG_JOYCNT: return gba->memory.io[address >> 1];
    case REG_JOY_RECV_LO: case REG_JOY_RECV_HI:
        if (mode != SERIAL_JOY) return 0;
        gba->memory.io[REG_JOYSTAT >> 1] &= ~2u;
        return gba->memory.io[address >> 1];
    case REG_JOYSTAT: return mode == SERIAL_JOY ? gba->memory.io[address >> 1] : 0;
    default: return 0;
    }
}

static void write_masked(struct CupidSerial* serial, uint32_t address, uint16_t value, uint16_t mask) {
    if (address >= GBA_SIZE_IO) return;
    struct CupidGBA* gba = serial->gba;
    enum CupidSerialMode before = cupid_serial_mode(serial);
    uint16_t* data = &gba->memory.io[address >> 1];
    switch (address) {
    case REG_SIOCNT: {
        bool wasStarted = (serial->control & CONTROL_START) != 0;
        serial->control = ((serial->control & ~mask) | (value & mask)) & 0x7FFF;
        enum CupidSerialMode mode = cupid_serial_mode(serial);
        if (serial->active && (mode != before || !(serial->control & CONTROL_START))) cancel_transfer(serial);
        *data = serial->control;
        update_outputs(serial);
        if (!wasStarted && (serial->control & CONTROL_START) &&
            (mode == SERIAL_NORMAL8 || mode == SERIAL_NORMAL32)) start_transfer(serial);
        return;
    }
    case REG_RCNT:
        serial->rcnt = ((serial->rcnt & ~mask) | (value & mask)) & 0xC1FF;
        if (serial->active && cupid_serial_mode(serial) != before) cancel_transfer(serial);
        *data = serial->rcnt;
        update_outputs(serial);
        return;
    case REG_SIODATA32_LO: case REG_SIODATA32_HI:
        if (before != SERIAL_NORMAL32) return;
        break;
    case REG_SIOMLT_SEND: break;
    case REG_JOYCNT: {
        uint16_t old = *data;
        *data = (old & ~(value & mask & 7) & 7) |
                (((old & ~mask) | (value & mask)) & 0x40);
        return;
    }
    case REG_JOY_TRANS_LO: case REG_JOY_TRANS_HI:
        if (before != SERIAL_JOY) return;
        gba->memory.io[REG_JOYSTAT >> 1] |= 8;
        break;
    case REG_JOYSTAT:
        if (before != SERIAL_JOY) return;
        mask &= 0x30;
        break;
    default: return;
    }
    *data = (*data & ~mask) | (value & mask);
}

void cupid_serial_write(struct CupidSerial* serial, uint32_t address, uint16_t value) {
    write_masked(serial, address, value, 0xFFFF);
}

void cupid_serial_write8(struct CupidSerial* serial, uint32_t address, uint8_t value) {
    unsigned shift = (address & 1) * 8;
    write_masked(serial, address & ~1u, (uint16_t)value << shift, (uint16_t)(0xFFu << shift));
}
