/**
 * @file bios.h
 * @brief GBA BIOS SWI (software interrupt) handling.
 *
 */

#ifndef CUPID_BIOS_H
#define CUPID_BIOS_H

#include "cupid/types.h"

struct CupidGBA;

/**
 * Handle SWI instruction: dispatches to the appropriate BIOS call.
 * All individual BIOS handlers are static in bios.c.
 */
void cupid_bios_swi(struct CupidGBA* gba, uint8_t comment);

/** HLE BIOS image — a minimal ROM that forwards SWI to the HLE handler */
extern const uint8_t cupid_hle_bios[];

#endif /* CUPID_BIOS_H */
