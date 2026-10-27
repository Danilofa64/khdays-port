/* Diagnostics on UWP (Xbox Dev Mode): diag.c's API without DbgHelp, which the
 * UWP does not offer. The stack and crash reports print without function
 * names, and the input script's KHDAYS_INPUT anchors (khdays_diag_symbol) do
 * not resolve: that is a desktop test tool. */
#include "runtime.h"

#include <stdio.h>

void khdays_diag_init(void)
{
    fprintf(stderr, "khdays-native: diagnostics without function names (UWP build)\n");
}

unsigned long long khdays_diag_symbol(const char *name)
{
    (void)name;
    return 0;
}

const char *khdays_diag_function_at(const void *address)
{
    (void)address;
    return NULL;
}

void khdays_diag_stack(void)
{
    fprintf(stderr, "khdays-native: stack walk not available on this build\n");
}

void khdays_diag_callers(char *out, int size, int skip)
{
    (void)skip;
    if (out != NULL && size > 0) {
        out[0] = '\0';
    }
}
