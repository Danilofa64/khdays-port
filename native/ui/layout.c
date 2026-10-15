/* The layout files (see layout.h). */
#include "layout.h"
#include "../gfx/elements.h"
#include "parts.h"
#include "../host/app/files.h"
#include "../runtime/core/rom.h"
#include "../runtime/core/session_log.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ANCHOR_TOP_LEFT, ANCHOR_TOP, ANCHOR_TOP_RIGHT, ANCHOR_LEFT, ANCHOR_CENTRE, ANCHOR_RIGHT,
       ANCHOR_BOTTOM_LEFT, ANCHOR_BOTTOM, ANCHOR_BOTTOM_RIGHT };
static const char *const anchors[9] = {"top-left", "top", "top-right", "left", "centre", "right",
                                       "bottom-left", "bottom", "bottom-right"};

typedef struct Entry {
    char *id;     /* an identifier, or a family's beginning (`family`) */
    int family;
    int at, anchor;
    float ax, ay;
    int move;
    float dx, dy;
    float scale;  /* 0: keep the DS's size */
    int has_order, order;
    int slot;     /* KE_SLOT's number and KES_SPRITES; -1: keep its own */
    int screen;   /* 0 the upper screen, 1 the lower; -1: keep its own */
    char *when;   /* the line applies while an element of that id is there (its section); NULL: always */
} Entry;

static Entry *entries;
static int entry_count, entry_room;
static int loaded;
static char *section; /* the file's section being read: `[when <id>]` */

/* the ids of this frame's elements (khdays_ui_layout_frame) */
static const char *const *present;
static int present_count;

static void say(const char *file, int line, const char *what, const char *text)
{
    fprintf(stderr, "ui layout: %s, line %d: %s: %s (the line is ignored)\n", file, line, what, text);
    khdays_log("ui layout: %s, line %d: %s: %s (the line is ignored)", file, line, what, text);
}

static int number(const char *s, float *out)
{
    char *end;
    const double v = strtod(s, &end);
    if (end == s || *end != '\0') {
        return 0;
    }
    *out = (float)v;
    return 1;
}

static int known_part(const char *id)
{
    const char *dot = strchr(id, '.');
    if (dot == NULL) {
        return 0;
    }
    for (int p = 0; p < KHDAYS_UI_PARTS; ++p) {
        const char *name = khdays_ui_part_name(p);
        if (strlen(name) == (size_t)(dot - id) && strncmp(id, name, (size_t)(dot - id)) == 0) {
            return 1;
        }
    }
    return 0;
}

static void read_line(const char *file, int line_no, char *line)
{
    char *words[16];
    int n = 0;
    Entry e;
    char *hash = strchr(line, '#');
    const char *original;
    static char shown[512];
    if (hash != NULL) {
        *hash = '\0';
    }
    snprintf(shown, sizeof(shown), "%s", line);
    original = shown;
    for (char *s = strtok(line, " \t\r"); s != NULL && n < 16; s = strtok(NULL, " \t\r")) {
        words[n++] = s;
    }
    if (n == 0) {
        return;
    }
    if (words[0][0] == '[') {
        /* a section header: `[when <id>]` or `[always]` */
        free(section);
        section = NULL;
        if (n == 1 && strcmp(words[0], "[always]") == 0) {
            return;
        }
        if (n == 2 && strcmp(words[0], "[when") == 0 && words[1][strlen(words[1]) - 1] == ']') {
            words[1][strlen(words[1]) - 1] = '\0';
            if (known_part(words[1]) && (section = (char *)malloc(strlen(words[1]) + 1)) != NULL) {
                strcpy(section, words[1]);
                return;
            }
        }
        say(file, line_no, "`[when <part>.<name>]` or `[always]` (the lines after it hold always)", original);
        return;
    }
    memset(&e, 0, sizeof(e));
    e.slot = -1;
    e.screen = -1;
    {
        const size_t len = strlen(words[0]);
        e.family = len > 0 && words[0][len - 1] == '*';
        if (!known_part(words[0])) {
            say(file, line_no, "not an element of a part (<part>.<name>)", original);
            return;
        }
    }
    for (int i = 1; i < n;) {
        const char *w = words[i];
        if (strcmp(w, "at") == 0 && i + 3 < n) {
            int a = -1;
            for (int k = 0; k < 9; ++k) {
                if (strcmp(words[i + 1], anchors[k]) == 0) a = k;
            }
            if (a < 0 || !number(words[i + 2], &e.ax) || !number(words[i + 3], &e.ay) || e.move) {
                say(file, line_no, "`at <anchor> <x> <y>` (and not with `move`)", original);
                return;
            }
            e.at = 1;
            e.anchor = a;
            i += 4;
        } else if (strcmp(w, "move") == 0 && i + 2 < n) {
            if (!number(words[i + 1], &e.dx) || !number(words[i + 2], &e.dy) || e.at) {
                say(file, line_no, "`move <dx> <dy>` (and not with `at`)", original);
                return;
            }
            e.move = 1;
            i += 3;
        } else if (strcmp(w, "scale") == 0 && i + 1 < n) {
            if (!number(words[i + 1], &e.scale) || e.scale <= 0.0f) {
                say(file, line_no, "`scale <s>`, s above 0", original);
                return;
            }
            i += 2;
        } else if (strcmp(w, "order") == 0 && i + 1 < n) {
            float o;
            if (!number(words[i + 1], &o)) {
                say(file, line_no, "`order <n>`", original);
                return;
            }
            e.has_order = 1;
            e.order = (int)o;
            i += 2;
        } else if (strcmp(w, "slot") == 0 && i + 1 < n) {
            const char *s = words[i + 1];
            if ((strncmp(s, "bg", 2) == 0 || strncmp(s, "obj", 3) == 0) && isdigit((unsigned char)s[strlen(s) - 1]) &&
                s[strlen(s) - 1] <= '3' && strlen(s) == (s[0] == 'b' ? 3u : 4u)) {
                e.slot = (s[0] == 'o' ? (int)KES_SPRITES : 0) | (s[strlen(s) - 1] - '0');
            } else {
                say(file, line_no, "`slot <bg0-bg3|obj0-obj3>`", original);
                return;
            }
            i += 2;
        } else if (strcmp(w, "screen") == 0 && i + 1 < n) {
            if (strcmp(words[i + 1], "upper") == 0 || strcmp(words[i + 1], "lower") == 0) {
                e.screen = words[i + 1][0] == 'l';
            } else {
                say(file, line_no, "`screen <upper|lower>`", original);
                return;
            }
            i += 2;
        } else {
            say(file, line_no, "not a layout word (at, move, scale, order, slot, screen)", original);
            return;
        }
    }
    if (entry_count == entry_room) {
        const int room = entry_room ? entry_room * 2 : 16;
        Entry *grown = (Entry *)realloc(entries, (size_t)room * sizeof(Entry));
        if (grown == NULL) {
            return;
        }
        entries = grown;
        entry_room = room;
    }
    e.id = (char *)malloc(strlen(words[0]) + 1);
    if (e.id == NULL) {
        return;
    }
    strcpy(e.id, words[0]);
    if (e.family) {
        e.id[strlen(e.id) - 1] = '\0';
    }
    if (section != NULL && (e.when = (char *)malloc(strlen(section) + 1)) != NULL) {
        strcpy(e.when, section);
    }
    entries[entry_count++] = e;
}

void khdays_ui_layout_read(const char *file, const char *text)
{
    int line_no = 0;
    const char *s = text;
    free(section); /* a file's lines apply always until its first section */
    section = NULL;
    while (*s != '\0') {
        const char *end = strchr(s, '\n');
        const size_t n = end != NULL ? (size_t)(end - s) : strlen(s);
        char *line = (char *)malloc(n + 1);
        ++line_no;
        if (line == NULL) {
            return;
        }
        memcpy(line, s, n);
        line[n] = '\0';
        read_line(file, line_no, line);
        free(line);
        s = end != NULL ? end + 1 : s + n;
    }
}

void khdays_ui_layout_reset(void)
{
    for (int i = 0; i < entry_count; ++i) {
#pragma warning(suppress: 6001) /* every item below the count was written before the count moved: none uninitialised */
        free(entries[i].id);
#pragma warning(suppress: 6001) /* every item below the count was written before the count moved: none uninitialised */
        free(entries[i].when);
    }
    entry_count = 0;
    loaded = 1;
}

/* Reads each part's file, through the mods' order. */
static void load(void)
{
    loaded = 1;
    for (int p = 0; p < KHDAYS_UI_PARTS; ++p) {
        char relative[64], path[1024];
        size_t room = 0x10000;
        char *text = NULL;
        long got = -1;
        snprintf(relative, sizeof(relative), "layouts/%s.txt", khdays_ui_part_name(p));
        if (!khdays_mods_find(relative, path, (int)sizeof(path))) {
            continue;
        }
        for (;;) {
            char *grown = (char *)realloc(text, room + 1);
            if (grown == NULL) {
                break;
            }
            text = grown;
            got = khdays_host_read_file(path, text, room);
            if (got < 0 || (size_t)got < room) {
                break;
            }
            room *= 2;
        }
        if (text != NULL && got >= 0) {
            text[got] = '\0';
            khdays_ui_layout_read(path, text);
            khdays_log("ui layout: %s read", path);
        } else {
            fprintf(stderr, "ui layout: %s cannot be read\n", path);
        }
        free(text);
    }
}

void khdays_ui_layout_frame(const char *const *ids, int count)
{
    present = ids;
    present_count = count;
}

/* Whether the entry's section applies this frame (its element is there). */
static int holds(const Entry *e)
{
    if (e->when == NULL) {
        return 1;
    }
    for (int i = 0; i < present_count; ++i) {
        if (present[i] != NULL && strcmp(present[i], e->when) == 0) {
            return 1;
        }
    }
    return 0;
}

static const Entry *entry_for(const char *id)
{
    const Entry *best = NULL;
    size_t best_len = 0;
    for (int i = entry_count - 1; i >= 0; --i) {
        const Entry *e = &entries[i];
        if (!e->family && strcmp(e->id, id) == 0 && holds(e)) {
            return e; /* the last line that names it wins */
        }
    }
    for (int i = entry_count - 1; i >= 0; --i) {
        const Entry *e = &entries[i];
        const size_t len = strlen(e->id);
        if (e->family && holds(e) && strncmp(e->id, id, len) == 0 && (best == NULL || len > best_len)) {
            best = e;
            best_len = len;
        }
    }
    return best;
}

int khdays_ui_layout_names(const char *id)
{
    if (!loaded) {
        load();
    }
    return id != NULL && entry_for(id) != NULL;
}

int khdays_ui_layout_place(const char *id, int *screen, int upper, int wide_margin, const float *group, float *x,
                           float *y, float *w, float *h, uint32_t *slot, int *order)
{
    const Entry *e;
    float margin;
    if (!loaded) {
        load();
    }
    e = id != NULL ? entry_for(id) : NULL;
    if (e != NULL && e->screen >= 0) {
        *screen = e->screen == 0 ? upper : !upper; /* (the engine shown on that screen) */
    }
    margin = *screen == 0 ? (float)wide_margin : 0.0f;
    if (e == NULL) {
        if (margin > 0.0f) {
            const float x0 = group != NULL ? group[0] : *x, x1 = group != NULL ? group[1] : *x + *w;
            if (x1 <= 128.0f) {
                *x -= margin;
            } else if (x0 >= 128.0f) {
                *x += margin;
            }
        }
        return 0;
    }
    if (e->scale > 0.0f) {
        *w *= e->scale;
        *h *= e->scale;
    }
    if (e->at) {
        const float left = -margin, right = 256.0f + margin, top = 0.0f, bottom = 192.0f;
        const int col = e->anchor % 3, row = e->anchor / 3;
        const float px = col == 0 ? left : col == 1 ? (left + right) / 2.0f : right;
        const float py = row == 0 ? top : row == 1 ? (top + bottom) / 2.0f : bottom;
        *x = px + e->ax;
        *y = py + e->ay;
    } else if (e->move) {
        *x += e->dx;
        *y += e->dy;
    }
    if (e->has_order) {
        *order = e->order;
    }
    if (e->slot >= 0) {
        *slot = (*slot & ~(KES_SPRITES | KES_NUMBER_MASK)) | (uint32_t)e->slot;
    }
    return 1;
}
