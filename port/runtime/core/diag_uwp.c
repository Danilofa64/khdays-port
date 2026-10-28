/* Diagnostics on UWP (Xbox Dev Mode): diag.c's API without DbgHelp, which the
 * UWP does not offer. The stack and crash reports print without function
 * names, and the input script's KHDAYS_INPUT anchors (khdays_diag_symbol) do
 * not resolve: that is a desktop test tool. */
#include "runtime.h"
#include "../menus/camp.h"
#include "../menus/charselect.h"
#include "../menus/cinelist.h"
#include "../net/roomlist.h"
#include "../../ui/situation.h"
#include "../input/input.h"
#include "window.h"

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

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

/* KHDAYS_TRACE_SCRIPT's printing needs the symbol names DbgHelp gives (diag.c); what the call is
 * also for stays: the field's event-script trace (runtime/movies/scene_trace.c) takes each step. */
void khdays_trace_script(void *st, void *entry)
{
    extern void khdays_scene_step(void *st); /* runtime/movies/scene_trace.c */
    (void)entry;
    khdays_scene_step(st);
}

/* Obj_UpdateAll's check (prepare.py HOOKS) of the next update an update returned: 0 keeps the current
 * one, -1 and -2 are markers, anything else must be native code. `fn` and `next` are function
 * references. diag.c's, without the names (and without KHDAYS_TRACE_UPDATES' printing): the screens
 * that follow the updates (the *_update_ran hooks) are told as on the desktop. */
void khdays_check_update(int fn, int next)
{
    extern char __ImageBase;
    const khdays_function_pointer native = khdays_function_lookup((KhdaysRef)fn);
    const IMAGE_NT_HEADERS *nt;
    const unsigned char *base = (const unsigned char *)&__ImageBase;
    khdays_function_pointer native_next;

    khdays_input_update_ran((khdays_uptr)native);
    khdays_cinelist_update_ran((khdays_uptr)native);
    khdays_roomlist_update_ran((khdays_uptr)native);
    khdays_charselect_update_ran((khdays_uptr)native);
    khdays_camp_update_ran((khdays_uptr)native);
    khdays_situation_update_ran((uintptr_t)native);

    if (next == 0 || next == -1 || next == -2) {
        return;
    }
    native_next = khdays_function_lookup((KhdaysRef)next);
    if (native_next == NULL) {
        fprintf(stderr,
                "khdays-native: the update %p returned 0x%08x as the object's next update, "
                "which is no function's reference\n",
                (const void *)native, (unsigned)next);
        fflush(stderr);
        exit(12);
    }
    nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
    if ((const unsigned char *)native_next >= base && (const unsigned char *)native_next < base + nt->OptionalHeader.SizeOfImage) {
        return;
    }
    fprintf(stderr,
            "khdays-native: the update %p returned 0x%08x as the object's next update, "
            "which is not code\n",
            (const void *)native, (unsigned)next);
    fflush(stderr);
    exit(12);
}
