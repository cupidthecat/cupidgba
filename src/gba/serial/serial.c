#include "cupid/gba.h"
#include "uart.h"

enum { CONTROL_START = 0x80, CONTROL_IRQ = 0x4000 };
/* Hardware-source timer observations include six clocks beyond the nominal
 * bit periods. Model status/data publication as a separate completion stage;
 * the electrical placement of this interval still needs pin measurements. */
enum { COMPLETION_CLOCKS = 6 };

/* Completion intervals by baud selector and final connected player slot.
 * These are the current word-transfer model; electrical edge placement and
 * transient cable behavior require separate qualification. */
static const unsigned multiplayerClocks[4][4] = {
    {31976, 63427, 94884, 125829},
    {8378, 16241, 24104, 31457},
    {5750, 10998, 16241, 20972},
    {3140, 5755, 8376, 10486}
};

static unsigned multiplayer_slot_clock(unsigned baud, unsigned player) {
    static const unsigned rates[] = {9600, 38400, 57600, 115200};
    /* Eighteen bits per slot include start/data/stop. These nominal phases
     * exclude unmeasured inter-player gaps; completion retains its separate
     * modeled interval and final missing-player timeout. */
    return (unsigned)((UINT64_C(16777216) * 18 * (player + 1) + rates[baud] / 2) / rates[baud]);
}

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

void cupid_serial_refresh_pins(struct CupidSerial* serial) {
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
        /* Command peers exchange whole frames without generating pin edges. */
        mask = SERIAL_SC | SERIAL_SD;
    } else if (mode == SERIAL_UART) {
        mask = SERIAL_SO | SERIAL_SD;
        if (serial->uart.outputHigh) levels |= SERIAL_SO;
        if (!cupid_serial_uart_ready(serial)) levels |= SERIAL_SD;
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
    if (serial->transferMode == SERIAL_MULTI) {
        /* Each receive slot was published by its own preceding phase. */
    } else if (serial->length32) {
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
    cupid_serial_refresh_pins(serial);
}

static void clock_edge(struct CupidSerial* serial, bool high) {
    serial->clockHigh = high;
    if (!high) {
        serial->outputHigh = (serial->transmit & (serial->length32 ? 0x80000000u : 0x80u)) != 0;
        serial->transmit <<= 1;
        cupid_serial_refresh_pins(serial);
        return;
    }
    serial->receive = (serial->receive << 1) | ((cupid_serial_pins(serial) & SERIAL_SI) != 0);
    --serial->remaining;
    cupid_serial_refresh_pins(serial);
    if (!serial->remaining) {
        cupid_timing_schedule(&serial->gba->timing, &serial->edgeEvent, COMPLETION_CLOCKS);
    }
}

static void edge_callback(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct CupidSerial* serial = context;
    if (!serial->active) return;
    if (serial->transferMode == SERIAL_MULTI && serial->multiplayerSlot <= serial->multiplayerLast) {
        const unsigned slot = serial->multiplayerSlot++;
        const unsigned elapsed = multiplayer_slot_clock(serial->multiplayerBaud, slot);
        serial->gba->memory.io[(REG_SIODATA32_LO >> 1) + slot] = serial->multiplayerData[slot];
        if (slot < serial->multiplayerLast) {
            const unsigned next = multiplayer_slot_clock(serial->multiplayerBaud, slot + 1);
            cupid_timing_schedule(timing, &serial->edgeEvent, (int32_t)(next - elapsed));
            return;
        }
        if (serial->multiplayerCompletion > elapsed) {
            cupid_timing_schedule(timing, &serial->edgeEvent, (int32_t)(serial->multiplayerCompletion - elapsed));
            return;
        }
    }
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
    serial->transferMode = cupid_serial_mode(serial);
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
    cupid_serial_refresh_pins(serial);
    if (serial->internalClock) {
        cupid_timing_schedule(&gba->timing, &serial->edgeEvent, (int32_t)serial->halfPeriod);
    }
}

static void schedule_multiplayer(struct CupidSerial* serial, unsigned baud) {
    unsigned last = 0;
    for (unsigned player = 1; player < 4; player++)
        if (serial->multiplayerPlayers & (1u << player)) last = player;
    serial->transferMode = SERIAL_MULTI;
    serial->active = true;
    serial->remaining = 0;
    serial->internalClock = serial->multiplayerId == 0;
    serial->multiplayerSlot = 0;
    serial->multiplayerLast = last;
    serial->multiplayerBaud = baud;
    serial->multiplayerCompletion = multiplayerClocks[baud][last];
    serial->control |= CONTROL_START;
    serial->gba->memory.io[REG_SIOCNT >> 1] = serial->control;
    for (unsigned player = 0; player < 4; player++)
        serial->gba->memory.io[(REG_SIODATA32_LO >> 1) + player] = 0xFFFF;
    cupid_timing_schedule(&serial->gba->timing, &serial->edgeEvent,
                          (int32_t)multiplayer_slot_clock(baud, 0));
}

static void start_multiplayer(struct CupidSerial* serial) {
    const unsigned baud = serial->control & 3;
    const uint16_t transmitted = serial->gba->memory.io[REG_SIOMLT_SEND >> 1];
    for (unsigned player = 0; player < 4; player++) serial->multiplayerData[player] = 0xFFFF;
    serial->multiplayerError = false;
    if (serial->multiplayerExchange)
        serial->multiplayerError = !serial->multiplayerExchange(serial->multiplayerContext,
            transmitted, baud, serial->multiplayerData, cupid_timing_global_time(&serial->gba->timing));
    for (unsigned player = 0; player < 4; player++)
        if (!(serial->multiplayerPlayers & (1u << player))) serial->multiplayerData[player] = 0xFFFF;
    serial->multiplayerData[0] = transmitted;
    schedule_multiplayer(serial, baud);
}

void cupid_serial_init(struct CupidSerial* serial, struct CupidGBA* gba) {
    memset(serial, 0, sizeof(*serial));
    serial->gba = gba;
    serial->edgeEvent = (struct CupidTimingEvent){.context = serial,
        .callback = edge_callback, .name = "Serial clock edge", .priority = 0x80};
    cupid_serial_uart_init(serial);
    cupid_serial_reset(serial);
}

void cupid_serial_reset(struct CupidSerial* serial) {
    cancel_transfer(serial);
    cupid_serial_uart_reset(serial);
    serial->control = 0;
    serial->rcnt = 0x8000;
    serial->transmit = serial->receive = 0;
    serial->transferMode = SERIAL_NORMAL8;
    serial->multiplayerError = false;
    memset(serial->multiplayerData, 0, sizeof(serial->multiplayerData));
    serial->multiplayerSlot = serial->multiplayerLast = 0;
    serial->multiplayerBaud = serial->multiplayerCompletion = 0;
    memset(&serial->gba->memory.io[REG_SIODATA32_LO >> 1], 0, 12);
    serial->gba->memory.io[REG_JOYCNT >> 1] = 0;
    memset(&serial->gba->memory.io[REG_JOY_RECV_LO >> 1], 0, 10);
    serial->gba->memory.io[REG_SIOCNT >> 1] = 0;
    serial->gba->memory.io[REG_RCNT >> 1] = serial->rcnt;
    cupid_serial_refresh_pins(serial);
}

bool cupid_serial_set_multiplayer_peer(struct CupidSerial* serial, unsigned playerId,
    uint8_t players, bool (*exchange)(void*, uint16_t, unsigned, uint16_t[4], uint64_t), void* context) {
    if (playerId > 3 || players > 15 || (players & (players + 1)) ||
        (players && !(players & (1u << playerId)))) return false;
    if (serial->active && serial->transferMode == SERIAL_MULTI) {
        cancel_transfer(serial);
        serial->control &= ~CONTROL_START;
        serial->gba->memory.io[REG_SIOCNT >> 1] = serial->control;
    }
    serial->multiplayerId = playerId;
    serial->multiplayerPlayers = players;
    serial->multiplayerExchange = exchange;
    serial->multiplayerContext = context;
    serial->multiplayerError = false;
    return true;
}

bool cupid_serial_multiplayer_receive(struct CupidSerial* serial, unsigned baud,
    const uint16_t received[4], bool error) {
    if (baud > 3 || !serial->multiplayerId || serial->active || serial->gba->stopped ||
        cupid_serial_mode(serial) != SERIAL_MULTI || !serial->multiplayerPlayers) return false;
    for (unsigned player = 0; player < 4; player++)
        serial->multiplayerData[player] = serial->multiplayerPlayers & (1u << player) ? received[player] : 0xFFFF;
    serial->multiplayerData[serial->multiplayerId] = serial->gba->memory.io[REG_SIOMLT_SEND >> 1];
    serial->multiplayerError = error;
    schedule_multiplayer(serial, baud);
    return true;
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
    cupid_serial_uart_input(serial, before, after);
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
        case SERIAL_MULTI:
            return (serial->control & 0x7F83) | 8 | (serial->multiplayerId << 4) |
                ((serial->multiplayerId || !serial->multiplayerPlayers) ? 4 : 0) |
                (serial->multiplayerError ? 0x40 : 0);
        case SERIAL_UART: return cupid_serial_uart_status(serial);
        default: return (serial->control & 0x7F8B) | (cupid_serial_pins(serial) & SERIAL_SI);
        }
    case REG_RCNT: return (serial->rcnt & 0xC1F0) | cupid_serial_pins(serial);
    case REG_SIODATA32_LO: case REG_SIODATA32_HI:
        return (mode == SERIAL_NORMAL32 || mode == SERIAL_MULTI) ? gba->memory.io[address >> 1] : 0;
    case REG_SIOMULTI2: case REG_SIOMULTI3:
        return mode == SERIAL_MULTI ? gba->memory.io[address >> 1] : 0;
    case REG_SIOMLT_SEND:
        return mode == SERIAL_UART ? cupid_serial_uart_read(serial) : gba->memory.io[address >> 1];
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
        uint16_t previous = serial->control;
        bool wasStarted = (serial->control & CONTROL_START) != 0;
        serial->control = ((serial->control & ~mask) | (value & mask)) & 0x7FFF;
        enum CupidSerialMode mode = cupid_serial_mode(serial);
        if (serial->active && (mode != before || !(serial->control & CONTROL_START))) cancel_transfer(serial);
        *data = serial->control;
        cupid_serial_uart_control(serial, before, previous);
        cupid_serial_refresh_pins(serial);
        if (mode == SERIAL_UART) return;
        if (!wasStarted && (serial->control & CONTROL_START)) {
            if (mode == SERIAL_NORMAL8 || mode == SERIAL_NORMAL32) start_transfer(serial);
            else if (mode == SERIAL_MULTI && !serial->multiplayerId && serial->multiplayerPlayers) start_multiplayer(serial);
            else if (mode == SERIAL_MULTI && !serial->multiplayerPlayers) {
                /* An unassigned endpoint retains the request latch, but has no
                 * cable clock owner. It cannot publish words or a completion. */
                serial->transferMode = SERIAL_MULTI;
                serial->active = true;
                serial->remaining = 0;
                serial->internalClock = false;
            }
            else if (mode == SERIAL_MULTI) {
                /* Once assigned as a child, Start is read-only. */
                serial->control &= ~CONTROL_START;
                *data = serial->control;
            }
        }
        return;
    }
    case REG_RCNT:
        serial->rcnt = ((serial->rcnt & ~mask) | (value & mask)) & 0xC1FF;
        if (serial->active && cupid_serial_mode(serial) != before) cancel_transfer(serial);
        *data = serial->rcnt;
        cupid_serial_uart_control(serial, before, serial->control);
        cupid_serial_refresh_pins(serial);
        return;
    case REG_SIODATA32_LO: case REG_SIODATA32_HI:
        if (before != SERIAL_NORMAL32) return;
        break;
    case REG_SIOMLT_SEND:
        if (before == SERIAL_UART) {
            if (mask & 0xFF) cupid_serial_uart_write(serial, (uint8_t)value);
            return;
        }
        break;
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
