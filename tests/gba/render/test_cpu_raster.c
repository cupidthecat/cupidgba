/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../../test.h"

enum { PIXELS = GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT, MAX_WORDS = 128 };

struct Program {
    uint32_t words[MAX_WORDS];
    struct { unsigned instruction, reg; uint32_t value; } literals[32];
    unsigned count, literalCount;
};

static unsigned emit(struct Program* p, uint32_t word) {
    CHECK(p->count < MAX_WORDS);
    p->words[p->count] = word;
    return p->count++;
}

static void literal(struct Program* p, unsigned reg, uint32_t value) {
    CHECK(p->literalCount < 32);
    unsigned index = p->literalCount++;
    p->literals[index].instruction = emit(p, 0);
    p->literals[index].reg = reg;
    p->literals[index].value = value;
}

static void branch(struct Program* p, unsigned instruction, unsigned target, unsigned condition) {
    int32_t displacement = (int32_t)target - (int32_t)instruction - 2;
    p->words[instruction] = condition << 28 | 0x0A000000 | ((uint32_t)displacement & 0xFFFFFF);
}

static void half(struct Program* p, uint32_t address, uint16_t value) {
    literal(p, 1, address);
    literal(p, 3, value);
    emit(p, 0xE1C130B0); /* STRH r3, [r1]. */
}

static void palette(struct Program* p, uint16_t value, bool word) {
    literal(p, 1, GBA_BASE_PALETTE);
    literal(p, 3, word ? value | (uint32_t)value << 16 : value);
    emit(p, word ? 0xE5813000 : 0xE1C130B0);
}

static void word_at(uint8_t* memory, unsigned offset, uint32_t value) {
    for (unsigned byte = 0; byte < 4; ++byte) memory[offset + byte] = (uint8_t)(value >> (byte * 8));
}

static void program(struct CupidGBA* gba, unsigned line, unsigned delay,
                    uint16_t initial, uint16_t changed, bool word) {
    struct Program p = {0};
    emit(&p, 0xE321F0D3); literal(&p, ARM_SP, 0x03007FE0); /* Supervisor stack for SWI. */
    emit(&p, 0xE321F0DF); literal(&p, ARM_SP, 0x03007F00); /* System mode, IRQ/FIQ masked. */
    literal(&p, 0, GBA_BASE_IO);
    literal(&p, 1, GBA_BASE_EWRAM); literal(&p, 3, 0x52535452); emit(&p, 0xE5813000);
    emit(&p, 0xE3A03000); emit(&p, 0xE5813004); /* Wake counter. */
    emit(&p, 0xE10F3000); emit(&p, 0xE5813008); /* Record CPSR before waiting. */
    half(&p, GBA_BASE_IO + REG_DISPSTAT, 8);
    half(&p, GBA_BASE_IO + REG_IE, IRQ_VBLANK);
    half(&p, GBA_BASE_IO + REG_IME, 0);
    unsigned firstSleep = emit(&p, 0), resume = p.count;
    literal(&p, 1, GBA_BASE_EWRAM + 4);
    emit(&p, 0xE5912000); emit(&p, 0xE2822001); emit(&p, 0xE5812000);
    palette(&p, initial, word);
    unsigned poll = p.count;
    emit(&p, 0xE1D020B6); emit(&p, 0xE3520000 | line); /* LDRH VCOUNT; CMP line. */
    unsigned again = emit(&p, 0); branch(&p, again, poll, 1);
    for (unsigned i = 0; i < delay; ++i) emit(&p, 0xE1A00000);
    palette(&p, changed, word);
    literal(&p, 1, GBA_BASE_EWRAM + 12); emit(&p, 0xE5812000);
    branch(&p, firstSleep, p.count, 14);
    half(&p, GBA_BASE_IO + REG_IF, IRQ_VBLANK); emit(&p, 0xE1A00000);
    /* HALTCNT rejects writes issued from ROM. Execute Halt through CupidGBA's
     * replacement BIOS, including its timed CPU/bus path and VBlank wake. */
    emit(&p, 0xEF020000);
    unsigned repeat = emit(&p, 0); branch(&p, repeat, resume, 14);
    for (unsigned i = 0; i < p.literalCount; ++i) {
        unsigned instruction = p.literals[i].instruction;
        unsigned offset = (p.count - instruction - 2) * 4;
        CHECK(offset <= 4095);
        p.words[instruction] = 0xE59F0000 | p.literals[i].reg << 12 | offset;
        emit(&p, p.literals[i].value);
    }
    gba->memory.rom = calloc(1, 0x4000);
    CHECK(gba->memory.rom);
    gba->memory.romSize = gba->memory.romImageSize = 0x4000;
    gba->memory.romMask = 0x3FFF;
    word_at(gba->memory.rom, 0, 0xEA00002E); /* Branch over the cartridge header. */
    for (unsigned i = 0; i < p.count; ++i) word_at(gba->memory.rom, 0xC0 + i * 4, p.words[i]);
    gba->cpu.cpsr.t = 0;
    _CupidSetMode(&gba->cpu, MODE_ARM);
    gba->cpu.gprs[ARM_PC] = GBA_BASE_ROM0;
    CupidARMWritePC(&gba->cpu);
}

static uint16_t rgb555(uint32_t pixel) {
    return (uint16_t)((pixel & 0xF8) >> 3 | (pixel & 0xF800) >> 6 | (pixel & 0xF80000) >> 9);
}

static unsigned scene(unsigned line, unsigned delay, uint16_t initial, uint16_t changed, bool word, const char* bios) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    uint16_t* previous = malloc(PIXELS * sizeof(*previous));
    CHECK(gba && previous);
    cupid_gba_create(gba);
    if (bios) CHECK(cupid_load_bios(gba, bios));
    cupid_gba_reset(gba);
    cupid_io_write(gba, REG_DISPCNT, 0);
    cupid_memory_store16(&gba->cpu, GBA_BASE_PALETTE, initial, NULL);
    program(gba, line, delay, initial, changed, word);
    for (unsigned frame = 1; frame <= 6; ++frame) {
        cupid_gba_run_frame(gba);
        if (frame == 5) for (unsigned i = 0; i < PIXELS; ++i) previous[i] = rgb555(gba->video.outputBuffer[i]);
    }
    CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_EWRAM, NULL) == 0x52535452);
    CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_EWRAM + 4, NULL) >= 4);
    CHECK((cupid_memory_load32(&gba->cpu, GBA_BASE_EWRAM + 8, NULL) & 255) == 0xDF);
    CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_EWRAM + 12, NULL) == line);
    unsigned firstChanged = GBA_SCREEN_WIDTH;
    for (unsigned y = 0; y < GBA_SCREEN_HEIGHT; ++y) for (unsigned x = 0; x < GBA_SCREEN_WIDTH; ++x) {
        unsigned i = y * GBA_SCREEN_WIDTH + x;
        uint16_t value = rgb555(gba->video.outputBuffer[i]);
        CHECK(value == previous[i]);
        if (initial == changed || y < line) CHECK(value == initial);
        else if (y > line) CHECK(value == changed);
        else {
            CHECK(value == initial || value == changed);
            if (value == changed && firstChanged == GBA_SCREEN_WIDTH) firstChanged = x;
            if (firstChanged != GBA_SCREEN_WIDTH) CHECK(value == changed);
        }
    }
    if (initial != changed) CHECK(firstChanged > 0 && firstChanged < GBA_SCREEN_WIDTH);
    free(previous);
    cupid_gba_destroy(gba); free(gba);
    return firstChanged;
}

int main(int argc, char** argv) {
    CHECK((argc == 2 || argc == 3) && (!strcmp(argv[1], "half") || !strcmp(argv[1], "word")));
    bool word = !strcmp(argv[1], "word");
    const char* bios = argc == 3 ? argv[2] : NULL;
    const unsigned lines[] = {1, 79, 80, 159};
    const uint16_t colors[] = {0x001F, 0x03E0, 0x7C00};
    unsigned count = 0;
    for (unsigned line = 0; line < 4; ++line) for (unsigned color = 0; color < 3; ++color) {
        uint16_t initial = colors[color], changed = colors[(color + 1) % 3];
        unsigned early = scene(lines[line], 0, initial, changed, word, bios);
        unsigned late = scene(lines[line], 7, initial, changed, word, bios);
        CHECK(late > early);
        scene(lines[line], 0, initial, initial, word, bios);
        count += 3;
    }
    printf("CPU raster scenes: %u; frames: %u; stable pixel pairs: %u\n", count, count * 6, count * PIXELS);
    return 0;
}
