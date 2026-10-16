/* khdays-thread-test: the execution contexts (context.h) the game's threads
 * run on. It creates 200 contexts (more than the 64 the port's table once
 * held). Each one, when switched to, counts its turn and switches back to the
 * first, three times over; then all are deleted. Prints "all thread checks
 * passed" and exits 0, or names each failure. */
#include "../host/app/context.h"

#include <stdio.h>

#define CONTEXTS 200
#define TURNS 3

static KhdaysContext *main_context;
static KhdaysContext *contexts[CONTEXTS];
static int turns[CONTEXTS];
static int order_ok = 1, last = -1;

static void run(void *argument)
{
    const int n = (int)(size_t)argument;
    for (;;) {
        if (khdays_context_current() != contexts[n]) {
            order_ok = 0;
        }
        ++turns[n];
        if (n != (last + 1) % CONTEXTS) {
            order_ok = 0;
        }
        last = n;
        khdays_context_switch(main_context);
    }
}

int main(void)
{
    int failures = 0;
    main_context = khdays_context_init();
    if (main_context == NULL) {
        fprintf(stderr, "thread_test: the main thread cannot become a context\n");
        return 1;
    }
    for (int n = 0; n < CONTEXTS; ++n) {
        contexts[n] = khdays_context_create(run, (void *)(size_t)n);
        if (contexts[n] == NULL) {
            fprintf(stderr, "thread_test: context %d not created\n", n);
            return 1;
        }
    }
    for (int t = 0; t < TURNS; ++t) {
        for (int n = 0; n < CONTEXTS; ++n) {
            khdays_context_switch(contexts[n]);
            if (khdays_context_current() != main_context) {
                fprintf(stderr, "thread_test: not back in the main context after %d\n", n);
                ++failures;
            }
        }
    }
    for (int n = 0; n < CONTEXTS; ++n) {
        if (turns[n] != TURNS) {
            fprintf(stderr, "thread_test: context %d ran %d turns, not %d\n", n, turns[n], TURNS);
            ++failures;
        }
        khdays_context_delete(contexts[n]);
    }
    if (!order_ok) {
        fprintf(stderr, "thread_test: the contexts did not run in the order they were switched to\n");
        ++failures;
    }
    if (failures == 0) {
        printf("all thread checks passed\n");
    }
    return failures != 0;
}
