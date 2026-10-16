/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _WIN32
#define _XOPEN_SOURCE 700
#endif
#include "context.h"
#include <stdbool.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <ucontext.h>
#endif

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define CUPID_CONTEXT_ASAN 1
#endif
#if __has_feature(thread_sanitizer)
#define CUPID_CONTEXT_TSAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define CUPID_CONTEXT_ASAN 1
#endif
#ifdef CUPID_CONTEXT_ASAN
#include <sanitizer/common_interface_defs.h>
#endif
#if defined(__SANITIZE_THREAD__)
#define CUPID_CONTEXT_TSAN 1
#endif
#ifdef CUPID_CONTEXT_TSAN
#include <sanitizer/tsan_interface.h>
#endif

enum { STACK_SIZE = 1024 * 1024 };
struct CupidContext {
    struct CupidContext* host;
    void (*entry)(void*);
    void* argument;
    void* stack;
    size_t stackSize;
    bool started, ending, finished;
#ifdef CUPID_CONTEXT_ASAN
    void* fakeStack;
#endif
#ifdef CUPID_CONTEXT_TSAN
    void* threadFiber;
#endif
#ifdef _WIN32
    void* fiber;
    bool converted;
#else
    ucontext_t state;
#endif
};

static _Thread_local struct CupidContext* initialContext;
static _Thread_local struct CupidContext* activeHost;

static void finish_switch(struct CupidContext* current, struct CupidContext* previous) {
#ifdef CUPID_CONTEXT_ASAN
    const void* bottom;
    size_t size;
    __sanitizer_finish_switch_fiber(current->fakeStack, &bottom, &size);
    previous->stack = (void*)bottom;
    previous->stackSize = size;
#else
    (void)current; (void)previous;
#endif
}

static void switch_context(struct CupidContext* from, struct CupidContext* to, bool final) {
#ifdef CUPID_CONTEXT_TSAN
    __tsan_switch_to_fiber(to->threadFiber, 0);
#endif
#ifdef CUPID_CONTEXT_ASAN
    __sanitizer_start_switch_fiber(final ? NULL : &from->fakeStack, to->stack, to->stackSize);
#else
    (void)final;
#endif
#ifdef _WIN32
    SwitchToFiber(to->fiber);
#else
    if (swapcontext(&from->state, &to->state) != 0) abort();
#endif
    finish_switch(from, to);
}

static void finish_context(struct CupidContext* context) {
    context->finished = true;
    switch_context(context, context->host, true);
    abort(); /* A completed stack is never resumed. */
}

#ifdef _WIN32
static void WINAPI context_entry(void* argument) {
    struct CupidContext* context = argument;
#else
static void context_entry(void) {
    struct CupidContext* context = initialContext;
#endif
    finish_switch(context, context->host);
    context->entry(context->argument);
    finish_context(context);
}

struct CupidContext* cupid_context_host_create(void) {
    if (activeHost) return NULL;
    struct CupidContext* host = calloc(1, sizeof(*host));
    if (!host) return NULL;
#ifdef _WIN32
    if (IsThreadAFiber()) { free(host); return NULL; }
    host->converted = true;
    host->fiber = ConvertThreadToFiberEx(NULL, FIBER_FLAG_FLOAT_SWITCH);
    if (!host->fiber) { free(host); return NULL; }
#endif
    activeHost = host;
#ifdef CUPID_CONTEXT_TSAN
    host->threadFiber = __tsan_get_current_fiber();
#endif
    return host;
}

struct CupidContext* cupid_context_create(struct CupidContext* host,
                                         void (*entry)(void*), void* argument) {
    if (!host || host != activeHost || !entry) return NULL;
    /* getcontext is a returns-twice checkpoint until makecontext replaces it.
     * Keep the allocation handle in memory across that checkpoint. */
    struct CupidContext* volatile context = calloc(1, sizeof(*context));
    if (!context) return NULL;
    context->host = host;
    context->entry = entry;
    context->argument = argument;
#ifdef _WIN32
    context->fiber = CreateFiberEx(64 * 1024, STACK_SIZE, FIBER_FLAG_FLOAT_SWITCH, context_entry, context);
    if (!context->fiber) { free(context); return NULL; }
#else
    context->stack = malloc(STACK_SIZE);
    context->stackSize = STACK_SIZE;
    if (!context->stack || getcontext(&context->state) != 0) {
        free(context->stack); free(context); return NULL;
    }
    context->state.uc_stack.ss_sp = context->stack;
    context->state.uc_stack.ss_size = context->stackSize;
    context->state.uc_link = NULL;
    /* The trampoline has no pointer-sized makecontext arguments. */
    makecontext(&context->state, context_entry, 0);
#endif
#ifdef CUPID_CONTEXT_TSAN
    context->threadFiber = __tsan_create_fiber(0);
#endif
    return context;
}

void cupid_context_resume(struct CupidContext* context) {
    if (context->finished) abort();
    initialContext = context;
    context->started = true;
    switch_context(context->host, context, false);
}

void cupid_context_yield(struct CupidContext* context) {
    switch_context(context, context->host, false);
    if (context->ending) finish_context(context);
}

void cupid_context_destroy(struct CupidContext* context) {
    if (!context) return;
    if (context->host) {
        if (context->started && !context->finished) {
            context->ending = true;
            cupid_context_resume(context);
        }
#ifdef _WIN32
        DeleteFiber(context->fiber);
#else
        free(context->stack);
#endif
#ifdef CUPID_CONTEXT_TSAN
        __tsan_destroy_fiber(context->threadFiber);
#endif
    } else {
#ifdef _WIN32
        if (context->converted && !ConvertFiberToThread()) abort();
#endif
        activeHost = NULL;
    }
    free(context);
}
