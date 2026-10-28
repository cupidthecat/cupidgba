/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Owned GPIO opcode-fetch controls. Cartridge, BIOS and battery files are
 * never loaded. Logical ARM A1 must not select another physical GPIO word. */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "cupid/registers.h"
#include "../../test.h"

static uint8_t input_pins(void *context) {
    return *(uint8_t *)context;
}
static void change_input(struct CupidTiming *timing, void *context, uint32_t late) {
    (void)timing;
    (void)late;
    *(uint8_t *)context = 9;
}
static void observe_branch(const struct CupidARM *cpu, void *context) {
    unsigned *count = context;
    CHECK((*count)++ == 0);
    CHECK(cpu->executionMode == MODE_THUMB);
    CHECK(cupid_arm_instruction_address(cpu) == GBA_BASE_IWRAM + 0x1000);
    CHECK(cpu->prefetch[0] == 0x4700);
}
static struct CupidGBA *machine(uint64_t epoch, uint8_t *image) {
    struct CupidGBA *g = calloc(1, sizeof(*g));
    CHECK(g);
    cupid_gba_create(g);
    cupid_gba_reset(g);
    cupid_timing_clear(&g->timing);
    g->timing.globalCycles = epoch;
    g->timing.masterCycles = (uint32_t)epoch;
    g->cpu.cpsr.i = 1;
    g->cpu.irqDisableLatch = true;
    g->memory.rom = image;
    CHECK(g->memory.rom);
    g->memory.romSize = GBA_SIZE_ROM;
    g->memory.romMask = GBA_SIZE_ROM - 1;
    return g;
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
    bool a1 = !strcmp(argv[1], "a1");
    CHECK(a1 || !strcmp(argv[1], "aligned"));
    const unsigned nw[] = {5, 4, 3, 9}, sw[3][2] = {{3, 2}, {5, 2}, {9, 2}};
    const uint64_t epochs[] = {0, INT32_MAX - UINT64_C(17), UINT32_MAX - UINT64_C(17)};
    unsigned scenes = 0;
    /* Every core borrows the same immutable owned image. Releasing the
     * borrowed pointer before destruction leaves one allocation to free. */
    uint8_t *image = calloc(1, GBA_SIZE_ROM);
    CHECK(image);
    const uint8_t imageBytes[] = {0x33, 0x44, 0x77, 0x88, 0xCC, 0xDD, 0x5A, 0xA5};
    memcpy(image + 0xC4, imageBytes, sizeof(imageBytes));
    memcpy(image + 0x10000C4, imageBytes, sizeof(imageBytes));
    for (unsigned region = 8; region < 14; ++region)
        for (unsigned n = 0; n < 4; ++n)
            for (unsigned s = 0; s < 2; ++s)
                for (unsigned prefetch = 0; prefetch < 2; ++prefetch)
                    for (unsigned epoch = 0; epoch < 3; ++epoch)
                        for (unsigned port = 0; port < 2; ++port)
                            for (unsigned readable = 0; readable < 2; ++readable)
                                for (unsigned entry = 0; entry < 3; ++entry) {
                                    struct CupidGBA *g = machine(epochs[epoch], image);
                                    uint8_t input = 5;
                                    cupid_cartridge_set_input(g, input_pins, &input);
                                    CHECK(cupid_cartridge_gpio_write(g, 0x080000C6, 3));
                                    CHECK(cupid_cartridge_gpio_write(g, 0x080000C4, 2));
                                    CHECK(cupid_cartridge_gpio_write(g, 0x080000C8,
                                                                     (uint16_t)readable));
                                    unsigned bank = (region - 8) / 2;
                                    cupid_io_write(g, REG_WAITCNT,
                                                   (uint16_t)(n << (2 + 3 * bank) |
                                                              s << (4 + 3 * bank) |
                                                              prefetch << 14));
                                    uint32_t target =
                                        (region << 24) + 0xC4 + 4 * port + (a1 ? 2 : 0);
                                    unsigned issued = 0;
                                    if (entry == 2) {
                                        const uint16_t code[] = {0x4700, 0x46C0, 0x46C0};
                                        memcpy(g->memory.iwram + 0x1000, code, sizeof(code));
                                        _CupidSetMode(&g->cpu, MODE_THUMB);
                                        g->cpu.memory.timed = true;
                                        g->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x1000;
                                        CupidThumbWritePC(&g->cpu);
                                        g->cpu.gprs[0] = (int32_t)target;
                                        g->cpu.instructionObserver = observe_branch;
                                        g->cpu.instructionObserverContext = &issued;
                                    } else {
                                        _CupidSetMode(&g->cpu, MODE_ARM);
                                        g->cpu.memory.timed = entry != 0;
                                        g->cpu.gprs[ARM_PC] = (int32_t)target;
                                    }
                                    uint64_t before = cupid_timing_global_time(&g->timing);
                                    struct CupidTimingEvent event = {
                                        .context = &input,
                                        .callback = change_input,
                                        .name = "GPIO alignment input edge",
                                        .priority = 0x60};
                                    if (entry && readable && !(region & 1) && !port)
                                        cupid_timing_schedule(&g->timing, &event,
                                                              (int32_t)(nw[n] + 1 + (entry == 2)));
                                    if (entry == 2)
                                        cupid_gba_step(g);
                                    else
                                        CupidARMWritePC(&g->cpu);
                                    bool selected = readable && !(region & 1);
                                    const uint32_t first = selected
                                                               ? (port ? 0xA55A0001 : 0x00030006)
                                                               : (port ? 0xA55ADDCC : 0x88774433);
                                    const uint32_t second =
                                        port ? 0 : (selected ? 0xA55A0001 : 0xA55ADDCC);
                                    uint64_t elapsed =
                                        cupid_timing_global_time(&g->timing) - before;
                                    uint64_t expected =
                                        entry ? nw[n] + 3 * sw[bank][s] + (entry == 2) : 0;
                                    if (g->cpu.prefetch[0] != first ||
                                        g->cpu.prefetch[1] != second || elapsed != expected)
                                        fprintf(stderr,
                                                "region=%x N=%u S=%u prefetch=%u epoch=%u port=%u "
                                                "readable=%u entry=%u A1=%u first=%08x/%08x "
                                                "second=%08x/%08x clocks=%llu/%llu\n",
                                                region, n, s, prefetch, epoch, port, readable,
                                                entry, a1 ? 2 : 0, g->cpu.prefetch[0], first,
                                                g->cpu.prefetch[1], second,
                                                (unsigned long long)elapsed,
                                                (unsigned long long)expected);
                                    CHECK(g->cpu.prefetch[0] == first &&
                                          g->cpu.prefetch[1] == second);
                                    CHECK(elapsed == expected);
                                    CHECK(g->cpu.executionMode == MODE_ARM &&
                                          (uint32_t)g->cpu.gprs[ARM_PC] == target + 4);
                                    CHECK(issued == (entry == 2));
                                    CHECK((g->cpu.memory.activeMask & 3) == 0);
                                    const uint8_t raw[] = {0x33, 0x44, 0x77, 0x88,
                                                           0xCC, 0xDD, 0x5A, 0xA5};
                                    CHECK(!memcmp(g->memory.rom + 0xC4, raw, sizeof(raw)));
                                    CHECK(!memcmp(g->memory.rom + 0x10000C4, raw, sizeof(raw)));
                                    g->memory.rom = NULL;
                                    cupid_gba_destroy(g);
                                    free(g);
                                    ++scenes;
                                }
    free(image);
    printf("PASS %s GPIO word-fetch scenes=%u\n", argv[1], scenes);
    return 0;
}
