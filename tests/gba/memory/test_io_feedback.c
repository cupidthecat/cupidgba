/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_io_feedback.c
 * @brief Readable I/O input publication and floating-port compatibility checks.
 *
 * - Observe completed CPU reads before a waiting DMA takes the bus.
 * - Keep rotated register results separate from aligned input samples.
 * - Preserve local RAM history and isolate untimed observer reads.
 */
#include "../../test.h"
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"

struct Port {
    uint32_t offset, value;
};

static const struct Port ports[] = {
    {0x000, 0x00010080}, {0x004, 0x00251200}, {0x008, 0xC2A51317},  {0x048, 0x02343F1A},
    {0x050, 0x10072A17}, {0x088, 0x00000200}, {0x0B8, 0x14000000},  {0x100, 0x00001623},
    {0x130, 0x015502AA}, {0x200, 0x13622569}, {0x204, 0x00004317},  {0x208, 0x00000001},
    {0x300, 0x00000001}, {0x800, 0x0D000020}, {0x10800, 0x0D000020}};

static struct CupidGBA *machine(uint64_t clock) {
    struct CupidGBA *g = calloc(1, sizeof(*g));
    CHECK(g);
    cupid_gba_create(g);
    cupid_gba_reset(g);
    g->cpu.cpsr.i = 1;
    g->cpu.irqDisableLatch = true;
    cupid_io_write(g, REG_DISPCNT, 0x0080);
    cupid_io_write(g, REG_GREENSWAP, 1);
    cupid_io_write(g, REG_DISPSTAT, 0x1200);
    cupid_io_write(g, REG_BG0CNT, 0x1317);
    cupid_io_write(g, REG_BG1CNT, 0xC2A5);
    cupid_io_write(g, REG_WININ, 0x3F1A);
    cupid_io_write(g, REG_WINOUT, 0x0234);
    cupid_io_write(g, REG_BLDCNT, 0x2A17);
    cupid_io_write(g, REG_BLDALPHA, 0x1007);
    cupid_io_write(g, REG_DMA0CNT_HI, 0x1400);
    cupid_io_write(g, REG_KEYCNT, 0x0155);
    cupid_io_write(g, REG_WAITCNT, 0x4317);
    g->video.vcount = 37;
    g->timers[0].counter = 0x1623;
    g->keysActive = 0x0155;
    g->ie = 0x2569;
    g->irqf = 0x1362;
    g->ime = 1;
    cupid_timing_clear(&g->timing);
    g->timing.globalCycles = clock;
    g->timing.masterCycles = (uint32_t)clock;
    g->cpu.prefetch[0] = 0x11112222;
    g->cpu.prefetch[1] = 0x33334444;
    g->cpu.memory.timed = true;
    return g;
}

static void destroy(struct CupidGBA *g) {
    cupid_gba_destroy(g);
    free(g);
}

static void start(struct CupidGBA *g, unsigned channel, uint32_t source, uint32_t destination) {
    cupid_dma_write_sad(g, channel, source);
    cupid_dma_write_dad(g, channel, destination);
    cupid_dma_write_cnt_lo(g, channel, 1);
    cupid_dma_write_cnt(g, channel, 0x8400);
}

static void prior_dma(struct CupidGBA *g) {
    cupid_memory_store32(&g->cpu, 0x02000200, (int32_t)UINT32_C(0xA7C45B36), NULL);
    start(g, 3, 0x02000200, 0x03000200);
    cupid_timing_tick(&g->timing, 11);
    CHECK(!g->memory.dmaRunning && g->memory.lastWasDMA);
    CHECK(g->memory.iwramBus == UINT32_C(0xA7C45B36));
}

static uint32_t read_port(struct CupidGBA *g, uint32_t address, unsigned width, int *cycles) {
    if (width == 1)
        return cupid_memory_load8(&g->cpu, address, cycles);
    if (width == 2)
        return cupid_memory_load16(&g->cpu, address, cycles);
    return cupid_memory_load32(&g->cpu, address, cycles);
}

static uint32_t sample(uint32_t word, unsigned lane, unsigned width) {
    if (width == 1)
        return ((word >> (lane * 8)) & 255) * UINT32_C(0x01010101);
    if (width == 2) {
        uint32_t half = (word >> ((lane & 2) * 8)) & UINT32_C(0xFFFF);
        return half | half << 16;
    }
    return word;
}

static uint32_t result(uint32_t word, unsigned lane, unsigned width) {
    if (width == 1)
        return (word >> (lane * 8)) & 255;
    if (width == 2)
        return ROR((word >> ((lane & 2) * 8)) & UINT32_C(0xFFFF), (lane & 1) * 8);
    return ROR(word, lane * 8);
}

/* Each pending DMA reads an unmapped I/O word after the CPU releases its
 * completed access. A stale preceding DMA latch must not win that handover. */
static void handover(unsigned width) {
    const uint64_t clocks[] = {0, INT32_MAX - 2u, UINT32_MAX - 2u, UINT64_C(0x300000000)};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; ++epoch)
        for (unsigned port = 0; port < sizeof(ports) / sizeof(ports[0]); ++port)
            for (unsigned lane = 0; lane < 4; ++lane)
                for (unsigned channel = 0; channel < 4; ++channel) {
                    struct CupidGBA *g = machine(clocks[epoch]);
                    prior_dma(g);
                    start(g, channel, 0x04001000, 0x02000100);
                    cupid_timing_tick(&g->timing, 1);
                    CHECK(!g->memory.dmaRunning);
                    uint32_t word = ports[port].value;
                    if (ports[port].offset == 0x0B8 && channel == 0)
                        word = 0x84000000;
                    const uint32_t expected = sample(word, lane, width);
                    int cycles = 0;
                    uint32_t value =
                        read_port(g, 0x04000000 + ports[port].offset + lane, width, &cycles);
                    CHECK(value == result(word, lane, width));
                    CHECK(cycles == 1);
                    CHECK(g->memory.inputBusValid && g->memory.openBus == expected);
                    CHECK(!g->memory.lastWasDMA && g->memory.dmaRunning);
                    CHECK(g->memory.iwramBus == UINT32_C(0xA7C45B36));
                    cupid_bus_wait(&g->cpu);
                    CHECK(!g->memory.dmaRunning && !g->cpuBlocked);
                    CHECK(cupid_memory_load32(&g->cpu, 0x02000100, NULL) == expected);
                    CHECK(g->memory.dma[channel].latch == expected);
                    CHECK(g->memory.lastWasDMA && cupid_memory_open_bus(&g->cpu) == expected);
                    CHECK(g->memory.iwramBus == UINT32_C(0xA7C45B36));
                    destroy(g);
                    ++scenes;
                }
    CHECK(scenes == 960);
    printf("readable I/O handovers width=%u scenes=%u\n", width, scenes);
}

static void floating(void) {
    const uint32_t offsets[] = {0x010, 0x054, 0x0A0, 0x0B0, 0x110, 0x138, 0x210, 0x304, 0x1000};
    unsigned scenes = 0;
    for (unsigned width = 1; width <= 4; width *= 2)
        for (unsigned port = 0; port < sizeof(offsets) / sizeof(offsets[0]); ++port)
            for (unsigned lane = 0; lane < 4; ++lane) {
                struct CupidGBA *g = machine(0);
                prior_dma(g);
                g->memory.openBus = UINT32_C(0x315AE297);
                g->memory.inputBusValid = true;
                int cycles = 0;
                CHECK(read_port(g, 0x04000000 + offsets[port] + lane, width, &cycles) ==
                      result(UINT32_C(0xA7C45B36), lane, width));
                /* The existing data-phase boundary ends the DMA indication.
                 * An undriven I/O read still must not sample a new CPU word. */
                CHECK(cycles == 1 && !g->memory.lastWasDMA);
                CHECK(g->memory.openBus == UINT32_C(0x315AE297) && g->memory.inputBusValid);
                CHECK(g->memory.iwramBus == UINT32_C(0xA7C45B36));
                destroy(g);
                ++scenes;
            }
    CHECK(scenes == 108);
    printf("floating I/O retained-input scenes=%u\n", scenes);
}

static void observers(void) {
    unsigned scenes = 0;
    for (unsigned width = 1; width <= 4; width *= 2)
        for (unsigned port = 0; port < sizeof(ports) / sizeof(ports[0]); ++port)
            for (unsigned lane = 0; lane < 4; ++lane) {
                struct CupidGBA *g = machine(0);
                prior_dma(g);
                g->memory.openBus = UINT32_C(0x315AE297);
                g->memory.inputBusValid = true;
                CHECK(read_port(g, 0x04000000 + ports[port].offset + lane, width, NULL) ==
                      result(ports[port].value, lane, width));
                CHECK(g->memory.lastWasDMA && g->memory.inputBusValid);
                CHECK(g->memory.openBus == UINT32_C(0x315AE297));
                CHECK(g->memory.iwramBus == UINT32_C(0xA7C45B36));
                destroy(g);
                ++scenes;
            }
    CHECK(scenes == 180);
    printf("untimed I/O observer scenes=%u\n", scenes);
}

/* The UART status read acknowledges its error and the data read consumes one
 * queued byte. Input publication must reuse those samples without rereading. */
static void read_once(void) {
    const uint64_t clocks[] = {0, INT32_MAX - 2u, UINT32_MAX - 2u, UINT64_C(0x300000000)};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; ++epoch)
        for (unsigned width = 1; width <= 4; width *= 2)
            for (unsigned lane = 0; lane < (width == 4 ? 4u : 2u); ++lane)
                for (unsigned channel = 0; channel < 4; ++channel) {
                    struct CupidGBA *g = machine(clocks[epoch]);
                    cupid_io_write(g, REG_RCNT, 0);
                    cupid_io_write(g, REG_SIOCNT, 0x3983);
                    CHECK(cupid_serial_mode(&g->serial) == SERIAL_UART);
                    prior_dma(g);
                    const uint8_t bytes[] = {0x5A, 0xC3, 0x96, 0x4D};
                    memcpy(g->serial.uart.receiveFIFO, bytes, sizeof(bytes));
                    g->serial.uart.receiveHead = 0;
                    g->serial.uart.receiveCount = 4;
                    g->serial.uart.error = true;
                    start(g, channel, 0x04001000, 0x02000100);
                    cupid_timing_tick(&g->timing, 1);
                    uint32_t word = width == 4 ? 0x005A39C3 : 0x005A0000;
                    unsigned portLane = width == 4 ? lane : 2 + lane;
                    const uint32_t expected = sample(word, portLane, width);
                    int cycles = 0;
                    CHECK(read_port(g, 0x04000128 + portLane, width, &cycles) ==
                          result(word, portLane, width));
                    CHECK(cycles == 1 && g->memory.inputBusValid);
                    CHECK(g->memory.openBus == expected && !g->memory.lastWasDMA);
                    CHECK(g->serial.uart.receiveCount == 3 && g->serial.uart.receiveHead == 1);
                    CHECK(g->serial.uart.error == (width != 4));
                    cupid_bus_wait(&g->cpu);
                    CHECK(cupid_memory_load32(&g->cpu, 0x02000100, NULL) == expected);
                    CHECK(g->memory.dma[channel].latch == expected);
                    CHECK(cupid_io_read(g, REG_SIOMLT_SEND) == 0xC3);
                    CHECK(g->serial.uart.receiveCount == 2 && g->serial.uart.receiveHead == 2);
                    CHECK(g->memory.openBus == expected && g->memory.lastWasDMA);
                    destroy(g);
                    ++scenes;
                }
    CHECK(scenes == 128);
    printf("single-sample UART I/O handovers=%u\n", scenes);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "byte"))
        handover(1);
    else if (!strcmp(argv[1], "halfword"))
        handover(2);
    else if (!strcmp(argv[1], "word"))
        handover(4);
    else if (!strcmp(argv[1], "floating"))
        RUN(floating);
    else if (!strcmp(argv[1], "observers"))
        RUN(observers);
    else if (!strcmp(argv[1], "read-once"))
        RUN(read_once);
    else
        return 2;
    return 0;
}
