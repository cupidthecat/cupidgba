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
 * Enter the SWI exception for the built-in firmware. The normalized service
 * byte does not gate entry; firmware reads the caller opcode and dispatches.
 */
void cupid_bios_swi(struct CupidGBA* gba, uint8_t comment);

/** Built-in BIOS image containing reset, exception and service instructions. */
extern const uint8_t cupid_hle_bios[];

#endif /* CUPID_BIOS_H */
