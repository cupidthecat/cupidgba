/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"

static void copy_window(const struct CupidPagedROM *page, uint8_t *view) {
    /* Mapping granularity is 512 bytes. One command covers at most fifteen
     * pages, and the complete transfer must fit the eight-KiB aperture. */
    if ((page->destination & 511) || (page->length & 511) || !page->length ||
        page->length >= GBA_SIZE_PAGED_WINDOW || page->destination >= GBA_SIZE_PAGED_WINDOW ||
        page->length > GBA_SIZE_PAGED_WINDOW - page->destination)
        return;
    if (page->source >= page->imageSize)
        return;
    size_t length = page->length;
    if (length > page->imageSize - page->source)
        length = page->imageSize - page->source;
    /* A short image leaves the unread tail of the old window unchanged. */
    memcpy(view + page->destination, page->image + page->source, length);
}

void cupid_paged_rom_reset(struct CupidPagedROM *page, uint8_t *view) {
    if (!page->image)
        return;
    page->command = 0;
    page->source = page->destination = 0;
    page->length = 0x1000;
    copy_window(page, view);
    page->source = 0x200;
    page->destination = 0x1000;
    copy_window(page, view);
}

bool cupid_paged_rom_write(struct CupidGBA *gba, uint32_t address, uint32_t value, unsigned width) {
    struct CupidPagedROM *page = &gba->memory.pagedROM;
    const unsigned region = address >> 24;
    if (!page->image || !page->length || region < GBA_REGION_ROM0 || region > GBA_REGION_ROM2_EX ||
        (address & 0x01FFFF00) != 0x00800100 ||
        (width != 4 && (width != 2 || region != GBA_REGION_ROM0)))
        return false;
    const unsigned offset = address & 0x3C;
    uint32_t *reg;
    switch (offset) {
        case 0:
            reg = &page->command;
            break;
        case 4:
            reg = &page->source;
            break;
        case 8:
            reg = &page->destination;
            break;
        case 12:
            reg = &page->length;
            break;
        default:
            return true;
    }
    /* The supported halfword port updates the low half of the selected
     * register, including its +2 alias; it does not select an upper lane. */
    if (width == 2)
        value = (value & 0xFFFF) | (*reg & 0xFFFF0000);
    switch (offset) {
        case 0:
            page->command = value;
            if (value == 1 || value == 0x11)
                copy_window(page, gba->memory.rom);
            break;
        case 4:
            page->source = value & 0x03FFFFFF;
            break;
        case 8:
            page->destination = value & 0x007FFFFF;
            break;
        case 12:
            if (value)
                page->length = value << 9;
            break;
    }
    return true;
}
