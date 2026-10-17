/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "uart.h"

enum {
    UART_CTS = 0x0004, UART_ODD = 0x0008, UART_FULL = 0x0010,
    UART_EMPTY = 0x0020, UART_ERROR = 0x0040, UART_EIGHT = 0x0080,
    UART_FIFO = 0x0100, UART_PARITY = 0x0200, UART_SEND = 0x0400,
    UART_RECEIVE = 0x0800, UART_IRQ = 0x4000
};

static const unsigned baudRates[] = {9600, 38400, 57600, 115200};

/* Nominal bit centers/boundaries come from the documented baud rate. Rounding
 * cumulative offsets avoids adding one rounding error for every bit. Physical
 * divider phase and oversampling require independent timing measurements. */
static unsigned offset(uint16_t control, unsigned halfBits) {
    unsigned divisor = baudRates[control & 3] * 2;
    return (unsigned)((UINT64_C(16777216) * halfBits + divisor - 1) / divisor);
}

static bool is_uart(const struct CupidSerial* serial) {
    return cupid_serial_mode(serial) == SERIAL_UART;
}

bool cupid_serial_uart_ready(const struct CupidSerial* serial) {
    if (!(serial->control & UART_RECEIVE)) return false;
    return serial->control & UART_FIFO ? serial->uart.receiveCount < 4 : !serial->uart.receiveReady;
}

static uint16_t status(const struct CupidSerial* serial) {
    const struct CupidUART* uart = &serial->uart;
    bool full = serial->control & UART_FIFO ? uart->transmitCount == 4
                                          : uart->transmitReady || uart->transmitting;
    bool empty = serial->control & UART_FIFO ? !uart->receiveCount : !uart->receiveReady;
    return (serial->control & 0x7F8F) | (full ? UART_FULL : 0)
        | (empty ? UART_EMPTY : 0) | (uart->error ? UART_ERROR : 0);
}

static void publish(struct CupidSerial* serial) {
    if (is_uart(serial)) serial->gba->memory.io[REG_SIOCNT >> 1] = status(serial);
}

static void interrupt(struct CupidSerial* serial) {
    if (serial->control & UART_IRQ)
        cupid_raise_irq_at(serial->gba, IRQ_SIO, cupid_timing_global_time(&serial->gba->timing));
}

static unsigned parity(uint8_t data) {
    data ^= data >> 4;
    data ^= data >> 2;
    data ^= data >> 1;
    return data & 1;
}

static void start_transmit(struct CupidSerial* serial) {
    struct CupidUART* uart = &serial->uart;
    if (!is_uart(serial) || serial->gba->stopped || uart->transmitting || !(serial->control & UART_SEND)) return;
    if ((serial->control & UART_CTS) && (cupid_serial_pins(serial) & SERIAL_SC)) return;
    bool fifo = (serial->control & UART_FIFO) != 0;
    if (fifo ? !uart->transmitCount : !uart->transmitReady) return;
    uint8_t data;
    bool emptied = false;
    if (fifo) {
        data = uart->transmitFIFO[uart->transmitHead];
        uart->transmitHead = (uart->transmitHead + 1) & 3;
        emptied = --uart->transmitCount == 0;
    } else {
        data = uart->transmitData;
        uart->transmitReady = false;
    }
    unsigned bits = serial->control & UART_EIGHT ? 8 : 7;
    data &= (uint8_t)((1u << bits) - 1);
    uart->transmitControl = serial->control;
    uart->transmitBits = 1 + bits + ((serial->control & UART_PARITY) != 0) + 1;
    uart->transmitFrame = (uint16_t)((unsigned)data << 1);
    if (serial->control & UART_PARITY)
        uart->transmitFrame |= (uint16_t)((parity(data) ^ ((serial->control & UART_ODD) != 0)) << (bits + 1));
    uart->transmitFrame |= (uint16_t)(1u << (uart->transmitBits - 1));
    uart->transmitStart = cupid_timing_global_time(&serial->gba->timing);
    uart->transmitStep = 1;
    uart->transmitting = true;
    uart->outputHigh = false; /* Start, LSB-first data, optional parity, high stop. */
    cupid_timing_schedule(&serial->gba->timing, &uart->transmitEvent, (int32_t)offset(uart->transmitControl, 2));
    publish(serial);
    if (emptied) interrupt(serial);
    cupid_serial_refresh_pins(serial);
}

static void transmit_callback(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct CupidSerial* serial = context;
    struct CupidUART* uart = &serial->uart;
    if (!is_uart(serial) || !uart->transmitting) return;
    if (uart->transmitStep < uart->transmitBits) {
        unsigned step = uart->transmitStep++;
        uart->outputHigh = ((uart->transmitFrame >> step) & 1) != 0;
        unsigned delay = offset(uart->transmitControl, uart->transmitStep * 2)
            - offset(uart->transmitControl, step * 2);
        /* Relative callback scheduling retains the original edge deadline. */
        cupid_timing_schedule(timing, &uart->transmitEvent, (int32_t)delay);
        cupid_serial_refresh_pins(serial);
    } else {
        bool fifo = (uart->transmitControl & UART_FIFO) != 0;
        uart->transmitting = false;
        uart->outputHigh = true;
        publish(serial);
        if (!fifo) interrupt(serial);
        start_transmit(serial);
        cupid_serial_refresh_pins(serial);
    }
}

static void finish_receive(struct CupidSerial* serial) {
    struct CupidUART* uart = &serial->uart;
    bool fifo = (uart->receiveControl & UART_FIFO) != 0;
    bool full = fifo ? uart->receiveCount == 4 : uart->receiveReady;
    uart->receiving = false;
    if (uart->receiveError || full) {
        /* Error frames never overwrite earlier unread data. */
        uart->error = true;
        interrupt(serial);
    } else if (fifo) {
        uart->receiveFIFO[(uart->receiveHead + uart->receiveCount) & 3] = uart->receiveShift;
        if (++uart->receiveCount == 4) interrupt(serial);
    } else {
        uart->receiveData = uart->receiveShift;
        uart->receiveReady = true;
        interrupt(serial);
    }
    publish(serial);
    cupid_serial_refresh_pins(serial);
}

static void receive_callback(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct CupidSerial* serial = context;
    struct CupidUART* uart = &serial->uart;
    if (!is_uart(serial) || !uart->receiving) return;
    bool high = (cupid_serial_pins(serial) & SERIAL_SI) != 0;
    unsigned step = uart->receiveStep;
    if (!step) {
        if (high) { /* A pulse shorter than the start-bit center is not a frame. */
            uart->receiving = false;
            return;
        }
    } else if (step <= uart->receiveBits) {
        if (high) uart->receiveShift |= (uint8_t)(1u << (step - 1));
    } else if ((uart->receiveControl & UART_PARITY) && step == uart->receiveBits + 1) {
        bool expected = (parity(uart->receiveShift) ^ ((uart->receiveControl & UART_ODD) != 0)) != 0;
        if (high != expected) uart->receiveError = true;
    } else {
        if (!high) uart->receiveError = true;
        finish_receive(serial);
        return;
    }
    ++uart->receiveStep;
    unsigned delay = offset(uart->receiveControl, uart->receiveStep * 2 + 1)
        - offset(uart->receiveControl, step * 2 + 1);
    cupid_timing_schedule(timing, &uart->receiveEvent, (int32_t)delay);
}

void cupid_serial_uart_init(struct CupidSerial* serial) {
    serial->uart.transmitEvent = (struct CupidTimingEvent){.context = serial,
        .callback = transmit_callback, .name = "UART transmit bit", .priority = 0x80};
    serial->uart.receiveEvent = (struct CupidTimingEvent){.context = serial,
        .callback = receive_callback, .name = "UART receive sample", .priority = 0x80};
}

void cupid_serial_uart_reset(struct CupidSerial* serial) {
    struct CupidUART* uart = &serial->uart;
    cupid_timing_deschedule(&serial->gba->timing, &uart->transmitEvent);
    cupid_timing_deschedule(&serial->gba->timing, &uart->receiveEvent);
    struct CupidTimingEvent tx = uart->transmitEvent, rx = uart->receiveEvent;
    *uart = (struct CupidUART){.transmitEvent = tx, .receiveEvent = rx, .outputHigh = true};
}

void cupid_serial_uart_control(struct CupidSerial* serial, enum CupidSerialMode before, uint16_t previous) {
    bool current = is_uart(serial);
    if (!current) {
        if (before == SERIAL_UART) cupid_serial_uart_reset(serial);
        return;
    }
    struct CupidUART* uart = &serial->uart;
    if (before != SERIAL_UART) cupid_serial_uart_reset(serial);
    serial->control &= 0x7F8F; /* Full, empty and error are generated status. */
    if (!(serial->control & UART_FIFO)) {
        uart->transmitHead = uart->transmitCount = 0;
        uart->receiveHead = uart->receiveCount = 0;
    }
    if ((previous ^ serial->control) & UART_FIFO) {
        uart->transmitReady = uart->receiveReady = false;
    }
    if (!(serial->control & UART_SEND)) {
        cupid_timing_deschedule(&serial->gba->timing, &uart->transmitEvent);
        uart->transmitting = false;
        uart->outputHigh = true;
    }
    if (!(serial->control & UART_RECEIVE)) {
        cupid_timing_deschedule(&serial->gba->timing, &uart->receiveEvent);
        uart->receiving = false;
    }
    publish(serial);
    start_transmit(serial);
}

void cupid_serial_uart_input(struct CupidSerial* serial, uint8_t previous, uint8_t current) {
    if (!is_uart(serial) || serial->gba->stopped) return;
    start_transmit(serial);
    struct CupidUART* uart = &serial->uart;
    if ((serial->control & UART_RECEIVE) && !uart->receiving
        && (previous & SERIAL_SI) && !(current & SERIAL_SI)) {
        uart->receiveControl = serial->control;
        uart->receiveStart = cupid_timing_global_time(&serial->gba->timing);
        uart->receiveStep = 0;
        uart->receiveBits = serial->control & UART_EIGHT ? 8 : 7;
        uart->receiveShift = 0;
        uart->receiveError = false;
        uart->receiving = true;
        cupid_timing_schedule(&serial->gba->timing, &uart->receiveEvent,
                              (int32_t)offset(uart->receiveControl, 1));
    }
}

void cupid_serial_uart_resume(struct CupidSerial* serial) {
    start_transmit(serial);
}

uint16_t cupid_serial_uart_status(struct CupidSerial* serial) {
    uint16_t value = status(serial);
    serial->uart.error = false;
    publish(serial);
    return value;
}

uint8_t cupid_serial_uart_read(struct CupidSerial* serial) {
    struct CupidUART* uart = &serial->uart;
    uint8_t value;
    if (serial->control & UART_FIFO) {
        if (!uart->receiveCount) return 0;
        value = uart->receiveFIFO[uart->receiveHead];
        uart->receiveHead = (uart->receiveHead + 1) & 3;
        --uart->receiveCount;
    } else {
        value = uart->receiveData;
        uart->receiveReady = false;
    }
    publish(serial);
    cupid_serial_refresh_pins(serial);
    return value;
}

void cupid_serial_uart_write(struct CupidSerial* serial, uint8_t value) {
    struct CupidUART* uart = &serial->uart;
    if (serial->control & UART_FIFO) {
        if (uart->transmitCount == 4) return;
        uart->transmitFIFO[(uart->transmitHead + uart->transmitCount) & 3] = value;
        ++uart->transmitCount;
    } else {
        if (uart->transmitReady || uart->transmitting) return;
        uart->transmitData = value;
        uart->transmitReady = true;
    }
    serial->gba->memory.io[REG_SIOMLT_SEND >> 1] = value;
    publish(serial);
    start_transmit(serial);
}
