/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file context.h
 * @brief Internal execution-context lifecycle and switch interface.
 *
 * - Declare context creation, suspension, resumption and destruction.
 * - Keep backend-specific context storage behind the interface.
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
