/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "context.h"
#include "../test.h"

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define CUPID_TEST_CONTEXT_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define CUPID_TEST_CONTEXT_ASAN 1
#endif
#ifdef CUPID_TEST_CONTEXT_ASAN
#include <sanitizer/asan_interface.h>
#endif

struct Probe { struct CupidContext* context; unsigned length, visits; };

static void local_stack(void* argument) {
    struct Probe* p = argument;
    /* A dynamic local remains on the real fiber stack, including when the
     * sanitizer also uses fake stacks for fixed-size local variables. */
    volatile unsigned length = p->length;
    unsigned char bytes[length];
    for (unsigned i = 0; i < length; ++i) bytes[i] = (unsigned char)(i * 7 + 3);
    for (unsigned visit = 0; visit < 4; ++visit) {
        for (unsigned i = 0; i < length; ++i) CHECK(bytes[i] == (unsigned char)(i * 7 + 3));
#ifdef CUPID_TEST_CONTEXT_ASAN
        int poisoned = __asan_address_is_poisoned(bytes + length);
        printf("length=%u visit=%u live boundary poisoned=%d\n", length, visit, poisoned);
        CHECK(poisoned);
#endif
        ++p->visits;
        cupid_context_yield(p->context);
    }
}

int main(void) {
    struct CupidContext* host = cupid_context_host_create(); CHECK(host);
    const unsigned lengths[] = {8, 17, 65};
    for (unsigned i = 0; i < 3; ++i) {
        struct Probe p = {.length = lengths[i]};
        p.context = cupid_context_create(host, local_stack, &p); CHECK(p.context);
        for (unsigned visit = 1; visit <= 4; ++visit) {
            cupid_context_resume(p.context); CHECK(p.visits == visit);
        }
        /* Complete the entry, then destroy its finished stack. */
        cupid_context_resume(p.context); CHECK(p.visits == 4);
        cupid_context_destroy(p.context);
        /* Cancellation must retire a suspended entry without running its
         * next body iteration or dropping sanitizer stack ownership. */
        p.visits = 0;
        p.context = cupid_context_create(host, local_stack, &p); CHECK(p.context);
        cupid_context_resume(p.context); CHECK(p.visits == 1);
        cupid_context_destroy(p.context); CHECK(p.visits == 1);
    }
    cupid_context_destroy(host);
    printf("PASS retained local data and live stack boundaries across yields, completion and cancellation\n");
    return 0;
}
