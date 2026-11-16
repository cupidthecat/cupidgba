/**
 * @file      joybus.c
 * @author    cupidthecat
 * @brief     JOY bus command processing and register transfer.
 * @date      2026-11-16
 *
 * - Decode reset, status, receive and transmit commands.
 * - Update command-visible data and interrupt state.
 *
 * @copyright Copyright (c) 2026 cupidthecat
 *
 * This file is part of CupidGBA.
 *
 * CupidGBA is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CupidGBA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CupidGBA.  If not, see <https://gnu.org>.
 */
#include "cupid/gba.h"

size_t cupid_serial_joybus_command(struct CupidSerial *serial, const uint8_t *request,
                                   size_t requestSize, uint8_t *response, size_t responseCapacity) {
    if (!request || !requestSize || !response || serial->gba->stopped ||
        cupid_serial_mode(serial) != SERIAL_JOY)
        return 0;

    const uint8_t command = request[0];
    size_t inputSize = 1, outputSize;
    switch (command) {
        case SERIAL_JOY_STATUS:
        case SERIAL_JOY_RESET:
            outputSize = 3;
            break;
        case SERIAL_JOY_READ:
            outputSize = 5;
            break;
        case SERIAL_JOY_WRITE:
            inputSize = 5;
            outputSize = 1;
            break;
        default:
            return 0;
    }
    /* A short packet or response buffer cannot partially publish a command. */
    if (requestSize != inputSize || responseCapacity < outputSize)
        return 0;

    struct CupidGBA *gba = serial->gba;
    uint16_t *control = &gba->memory.io[REG_JOYCNT >> 1];
    uint16_t *status = &gba->memory.io[REG_JOYSTAT >> 1];
    uint8_t reply[5];
    switch (command) {
        case SERIAL_JOY_STATUS:
        case SERIAL_JOY_RESET:
            reply[0] = 0x00;
            reply[1] = 0x04;
            reply[2] = (uint8_t)*status;
            if (command == SERIAL_JOY_RESET)
                *control |= 1;
            break;
        case SERIAL_JOY_WRITE:
            /* Capture all four bytes before touching an overlapping reply buffer. */
            gba->memory.io[REG_JOY_RECV_LO >> 1] = request[1] | (uint16_t)request[2] << 8;
            gba->memory.io[REG_JOY_RECV_HI >> 1] = request[3] | (uint16_t)request[4] << 8;
            *control |= 2;
            *status |= 2;
            reply[0] = (uint8_t)*status;
            break;
        case SERIAL_JOY_READ: {
            const uint16_t low = gba->memory.io[REG_JOY_TRANS_LO >> 1];
            const uint16_t high = gba->memory.io[REG_JOY_TRANS_HI >> 1];
            reply[0] = (uint8_t)low;
            reply[1] = (uint8_t)(low >> 8);
            reply[2] = (uint8_t)high;
            reply[3] = (uint8_t)(high >> 8);
            /* The response captures status before acknowledging the send buffer. */
            reply[4] = (uint8_t)*status;
            *control |= 4;
            *status &= ~8u;
            break;
        }
        default:
            return 0;
    }
    memcpy(response, reply, outputSize);
    if (command != SERIAL_JOY_STATUS && (*control & 0x40)) {
        cupid_raise_irq_at(gba, IRQ_SIO, cupid_timing_global_time(&gba->timing));
    }
    return outputSize;
}
