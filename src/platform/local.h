/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_LOCAL_H
#define CUPID_LOCAL_H
#include "cupid/platform.h"
int cupid_platform_run_local(const char* const paths[], unsigned count, const char* bios,
                             const char* saveType, int scale, unsigned frames);
bool cupid_platform_local_save_path(const char* rom, unsigned player, char* out, size_t capacity);
struct CupidLocalFrames { uint32_t* pixels[4]; };
void cupid_platform_capture_local_frame(void* context, unsigned player, const uint32_t* pixels, uint64_t timestamp);
bool cupid_platform_render_local(struct CupidPlatform* platform, SDL_Texture* const textures[],
                                 const uint32_t* const frames[], unsigned count, unsigned focus);
#endif
