/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      context.h
 * @author    cupidthecat
 * @brief     Internal execution-context lifecycle and switch interface.
 * @date      2026-11-16
 *
 * - Declare context creation, suspension, resumption and destruction.
 * - Keep backend-specific context storage behind the interface.
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
#ifndef CUPID_CONTEXT_H
#define CUPID_CONTEXT_H

/* Cooperative stacks stay on the creating thread. A clock request can suspend
 * a CPU in its bus wait without unwinding or replaying that instruction. */
struct CupidContext;
/* Host handles are shared on the creating thread. Each successful acquisition
 * needs one destroy, after destroying that owner's child stacks. */
struct CupidContext *cupid_context_host_create(void);
struct CupidContext *cupid_context_create(struct CupidContext *host, void (*entry)(void *),
                                          void *argument);
void cupid_context_resume(struct CupidContext *context);
void cupid_context_yield(struct CupidContext *context);
void cupid_context_destroy(struct CupidContext *context);
#endif
