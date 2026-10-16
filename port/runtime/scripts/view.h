/* What the settings menu's Mods page shows of the mods: a copy the game's thread makes at the frame
 * point when something changed, and the window's thread takes (queue.h carries the player's changes
 * back). */
#ifndef KHDAYS_SCRIPTS_VIEW_H
#define KHDAYS_SCRIPTS_VIEW_H

#include "mod.h"

typedef struct KhdaysOptionView {
    char *key;
    int kind; /* KhdaysOptionKind */
    char *label[KHDAYS_OPTION_LANGUAGES];
    char *description[KHDAYS_OPTION_LANGUAGES];
    char **values;
    int value_count;
    double low, high, step, value;
} KhdaysOptionView;

typedef struct KhdaysModView {
    char *folder, *id, *name, *version, *author, *description, *reason;
    int kind;  /* KhdaysModKind; -1 when its manifest was refused */
    int state; /* KhdaysModState */
    int enabled;
    KhdaysOptionView *options;
    int option_count;
} KhdaysModView;

typedef struct KhdaysModsView {
    unsigned serial; /* 0: never taken */
    KhdaysModView *mods;
    int count;
} KhdaysModsView;

/* The game's thread: the mods as they are now, for the window (scripts.c calls it when they change).
 * `now`'s strings are copied. */
void khdays_scripts_view_set(const KhdaysModsView *now);

/* The window's thread: replaces `view` by the latest copy when it is newer than `view->serial`.
 * Returns 1 when it did. */
int khdays_scripts_view(KhdaysModsView *view);

void khdays_scripts_view_free(KhdaysModsView *view);

/* A text of a label or description in a language (1..5), else the one for every language, else
 * English, else any. NULL when there is none. */
const char *khdays_scripts_text(char *const texts[KHDAYS_OPTION_LANGUAGES], int language);

#endif
