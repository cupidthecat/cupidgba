/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _WIN32
#define _XOPEN_SOURCE 700
#endif
#include "context.h"
#include <stdbool.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#elif defined(CUPID_USE_FCONTEXT)
/* Native context entry points use the platform C calling convention. The
 * transfer returns the suspended caller and the pointer it supplied. */
struct CupidContextTransfer { void* continuation; void* argument; };
extern struct CupidContextTransfer jump_fcontext(void* destination, void* argument);
extern void* make_fcontext(void* stackTop, size_t stackSize,
                          void (*entry)(struct CupidContextTransfer));
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
    unsigned references, children;
#ifdef CUPID_CONTEXT_ASAN
    void* fakeStack;
#endif
#ifdef CUPID_CONTEXT_TSAN
    void* threadFiber;
#endif
#ifdef _WIN32
    void* fiber;
    bool converted;
#elif defined(CUPID_USE_FCONTEXT)
    void* continuation;
#else
    ucontext_t state;
#endif
};

static _Thread_local struct CupidContext* initialContext;
static _Thread_local struct CupidContext* activeHost;
static _Thread_local struct CupidContext* runningContext;

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
    struct CupidContext* previous = to;
    if (runningContext != from) abort();
    runningContext = to;
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
#elif defined(CUPID_USE_FCONTEXT)
    struct CupidContextTransfer transfer = jump_fcontext(to->continuation, from);
    previous = transfer.argument;
    previous->continuation = transfer.continuation;
#else
    if (swapcontext(&from->state, &to->state) != 0) abort();
#endif
    finish_switch(from, previous);
}

static void finish_context(struct CupidContext* context) {
    context->finished = true;
    switch_context(context, context->host, true);
    abort(); /* A completed stack is never resumed. */
}

#ifdef _WIN32
static void WINAPI context_entry(void* argument) {
    struct CupidContext* context = argument;
#elif defined(CUPID_USE_FCONTEXT)
static void context_entry(struct CupidContextTransfer transfer) {
    struct CupidContext* context = initialContext;
    struct CupidContext* previous = transfer.argument;
    previous->continuation = transfer.continuation;
#else
static void context_entry(void) {
    struct CupidContext* context = initialContext;
#endif
#if defined(CUPID_USE_FCONTEXT) && !defined(_WIN32)
    finish_switch(context, previous);
#else
    finish_switch(context, context->host);
#endif
    context->entry(context->argument);
    finish_context(context);
}

struct CupidContext* cupid_context_host_create(void) {
    /* Independent cores and a local cable may own stacks on the same thread.
     * Their host remains valid until its last owner and child are released. */
    if (activeHost) {
        if (runningContext != activeHost) return NULL;
        ++activeHost->references;
        return activeHost;
    }
    struct CupidContext* host = calloc(1, sizeof(*host));
    if (!host) return NULL;
#ifdef _WIN32
    if (IsThreadAFiber()) { free(host); return NULL; }
    host->converted = true;
    host->fiber = ConvertThreadToFiberEx(NULL, FIBER_FLAG_FLOAT_SWITCH);
    if (!host->fiber) { free(host); return NULL; }
#endif
    activeHost = host;
    runningContext = host;
    host->references = 1;
#ifdef CUPID_CONTEXT_TSAN
    host->threadFiber = __tsan_get_current_fiber();
#endif
    return host;
}

struct CupidContext* cupid_context_create(struct CupidContext* host,
                                         void (*entry)(void*), void* argument) {
    if (!host || host != activeHost || runningContext != host || !entry) return NULL;
    /* The portable backend's getcontext is a returns-twice checkpoint until
     * makecontext replaces it. Keep its allocation handle in memory. */
    struct CupidContext* volatile context = calloc(1, sizeof(*context));
    if (!context) return NULL;
    context->host = host;
    context->entry = entry;
    context->argument = argument;
#ifdef _WIN32
    context->fiber = CreateFiberEx(64 * 1024, STACK_SIZE, FIBER_FLAG_FLOAT_SWITCH, context_entry, context);
    if (!context->fiber) { free(context); return NULL; }
#elif defined(CUPID_USE_FCONTEXT)
    context->stack = malloc(STACK_SIZE);
    context->stackSize = STACK_SIZE;
    if (!context->stack) { free(context); return NULL; }
    context->continuation = make_fcontext((char*)context->stack + STACK_SIZE, STACK_SIZE, context_entry);
    if (!context->continuation) { free(context->stack); free(context); return NULL; }
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
#ifdef CUPID_CONTEXT_ASAN
    /* The fiber interface tracks these stacks. Disable makecontext's generic
     * stack-clearing marker so swapcontext retains live frame redzones. */
    context->state.uc_stack.ss_flags = 0;
#endif
#endif
#ifdef CUPID_CONTEXT_TSAN
    context->threadFiber = __tsan_create_fiber(0);
#endif
    ++host->children;
    return context;
}

void cupid_context_resume(struct CupidContext* context) {
    if (context->finished || runningContext != context->host) abort();
    initialContext = context;
    context->started = true;
    switch_context(context->host, context, false);
}

void cupid_context_yield(struct CupidContext* context) {
    if (runningContext != context) abort();
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
        --context->host->children;
    } else {
        if (context != activeHost || runningContext != context || !context->references) abort();
        if (--context->references) return;
        if (context->children) abort();
#ifdef _WIN32
        if (context->converted && !ConvertFiberToThread()) abort();
#endif
        activeHost = NULL;
        runningContext = NULL;
    }
    free(context);
}
