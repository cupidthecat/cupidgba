#ifndef CUPID_SERIAL_H
#define CUPID_SERIAL_H

#include "cupid/timing.h"

struct CupidGBA;

enum CupidSerialPin {
    SERIAL_SC = 1,
    SERIAL_SD = 2,
    SERIAL_SI = 4,
    SERIAL_SO = 8
};

enum CupidSerialMode {
    SERIAL_NORMAL8,
    SERIAL_NORMAL32,
    SERIAL_MULTI,
    SERIAL_UART,
    SERIAL_GPIO,
    SERIAL_JOY
};

enum CupidJoyCommand {
    SERIAL_JOY_STATUS = 0x00,
    SERIAL_JOY_READ = 0x14,
    SERIAL_JOY_WRITE = 0x15,
    SERIAL_JOY_RESET = 0xFF
};

struct CupidUART {
    struct CupidTimingEvent transmitEvent, receiveEvent;
    uint64_t transmitStart, receiveStart;
    uint16_t transmitControl, receiveControl, transmitFrame;
    unsigned transmitStep, transmitBits, receiveStep, receiveBits;
    uint8_t transmitFIFO[4], receiveFIFO[4];
    unsigned transmitHead, transmitCount, receiveHead, receiveCount;
    uint8_t transmitData, receiveData, receiveShift;
    bool transmitReady, receiveReady, transmitting, receiving;
    bool outputHigh, error, receiveError;
};

struct CupidSerial {
    struct CupidGBA* gba;
    struct CupidTimingEvent edgeEvent;
    uint16_t control;
    uint16_t rcnt;
    uint32_t transmit;
    uint32_t receive;
    unsigned remaining;
    unsigned halfPeriod;
    bool active;
    bool internalClock;
    bool clockHigh;
    bool outputHigh;
    bool length32;
    enum CupidSerialMode transferMode;
    unsigned multiplayerId;
    uint8_t multiplayerPlayers;
    bool multiplayerError;
    uint16_t multiplayerData[4];
    unsigned multiplayerSlot;
    unsigned multiplayerLast;
    unsigned multiplayerBaud;
    unsigned multiplayerCompletion;
    uint8_t inputMask;
    uint8_t inputLevels;
    uint8_t outputMask;
    uint8_t outputLevels;
    enum CupidSerialMode outputMode;

    /* Reports driven levels, directions and mode changes at emulated timestamps.
     * A connected device may change its inputs synchronously in this callback. */
    void (*outputChanged)(void* context, uint8_t mask, uint8_t levels, uint64_t timestamp);
    void* outputContext;

    /* A multiplayer device exchanges one latched 16-bit word per player.
     * Returning false reports a transfer error. Receive slots publish in wire
     * order while normal peripheral clocks run. Subframe phase estimates and
     * electrical timing require separate qualification. */
    bool (*multiplayerExchange)(void* context, uint16_t transmitted, unsigned baud,
                                uint16_t received[4], uint64_t timestamp);
    void* multiplayerContext;
    /* Optional local-session policies. Ready is queried without advancing a
     * clock. Cancellation is reported after releasing this endpoint's frame. */
    bool (*multiplayerReady)(void* context);
    void (*multiplayerCancelled)(void* context);
    struct CupidUART uart;
};

void cupid_serial_init(struct CupidSerial* serial, struct CupidGBA* gba);
void cupid_serial_reset(struct CupidSerial* serial);
enum CupidSerialMode cupid_serial_mode(const struct CupidSerial* serial);
uint16_t cupid_serial_read(struct CupidSerial* serial, uint32_t address);
void cupid_serial_write(struct CupidSerial* serial, uint32_t address, uint16_t value);
void cupid_serial_write8(struct CupidSerial* serial, uint32_t address, uint8_t value);

/* Call at the device's current emulated timestamp, after dispatching preceding
 * events. Bits outside mask are undriven. GPIO inputs have pull-up levels. */
void cupid_serial_set_input(struct CupidSerial* serial, uint8_t mask, uint8_t levels);
uint8_t cupid_serial_pins(const struct CupidSerial* serial);
void cupid_serial_set_output_callback(struct CupidSerial* serial,
    void (*callback)(void*, uint8_t, uint8_t, uint64_t), void* context);

/* Configure a device at the current emulated timestamp. Player masks contain
 * a contiguous cable chain (1, 3, 7 or 15); zero disconnects it. Reconfiguration
 * cancels an active multiplayer frame. This does not advance another core. */
bool cupid_serial_set_multiplayer_peer(struct CupidSerial* serial, unsigned playerId,
    uint8_t players, bool (*exchange)(void*, uint16_t, unsigned, uint16_t[4], uint64_t), void* context);

/* Start a slave's incoming frame after dispatching preceding events to the
 * same timestamp as its master. The caller owns synchronization between cores. */
bool cupid_serial_multiplayer_receive(struct CupidSerial* serial, unsigned baud,
    const uint16_t received[4], bool error);

/* Complete one JOY command at the peer's current emulated timestamp, after
 * dispatching preceding events. The peer owns frame timing and synchronization;
 * this entry point processes complete command bytes, without generating pins.
 * Request sizes are one byte, or five bytes for WRITE. Replies contain three
 * bytes for STATUS/RESET, five for READ, or one for WRITE. Returns zero without
 * changing state or response on invalid buffers/lengths, another mode, or STOP.
 * Request and response buffers may overlap. */
size_t cupid_serial_joybus_command(struct CupidSerial* serial,
    const uint8_t* request, size_t requestSize, uint8_t* response, size_t responseCapacity);

#endif
