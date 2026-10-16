/* The parts and their switches (parts.h). */
#include "parts.h"
#include "../runtime/core/session_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const names[KHDAYS_UI_PARTS] = {"text", "hud", "pause", "camp", "save", "mission", "shop"};
static const char *const mode_names[3] = {"off", "check", "on"};

/* Each part's mode when KHDAYS_UI does not set it: off until the part is
 * verified, then on. */
static const int defaults[KHDAYS_UI_PARTS] = {
    KHDAYS_UI_ON, KHDAYS_UI_ON, KHDAYS_UI_ON, KHDAYS_UI_OFF, KHDAYS_UI_OFF, KHDAYS_UI_OFF, KHDAYS_UI_OFF,
};

static int modes[KHDAYS_UI_PARTS];
static int ready;

static int mode_named(const char *s, size_t n)
{
    for (int m = 0; m < 3; ++m) {
        if (strlen(mode_names[m]) == n && strncmp(s, mode_names[m], n) == 0) {
            return m;
        }
    }
    return -1;
}

static int part_named(const char *s, size_t n)
{
    for (int p = 0; p < KHDAYS_UI_PARTS; ++p) {
        if (strlen(names[p]) == n && strncmp(s, names[p], n) == 0) {
            return p;
        }
    }
    return -1;
}

void khdays_ui_parts_init(void)
{
    const char *setting = getenv("KHDAYS_UI");
    char line[256];
    size_t at = 0;
    if (ready) {
        return;
    }
    ready = 1;
    memcpy(modes, defaults, sizeof(modes));
    for (const char *s = setting; s != NULL && *s != '\0';) {
        const char *end = strchr(s, ',');
        const size_t n = end != NULL ? (size_t)(end - s) : strlen(s);
        const char *eq = memchr(s, '=', n);
        int part = -2, mode = -1;
        if (eq != NULL) {
            const size_t pn = (size_t)(eq - s);
            part = pn == 3 && strncmp(s, "all", 3) == 0 ? -1 : part_named(s, pn);
            mode = mode_named(eq + 1, n - pn - 1);
        }
        if (part == -2 || mode < 0) {
            fprintf(stderr, "ui: KHDAYS_UI: \"%.*s\" is not <part>=<off|check|on> (parts: text, hud, pause, camp, "
                            "save, mission, shop, all)\n",
                    (int)n, s);
        } else if (part == -1) {
            for (int p = 0; p < KHDAYS_UI_PARTS; ++p) {
                modes[p] = mode;
            }
        } else {
            modes[part] = mode;
        }
        s = end != NULL ? end + 1 : NULL;
    }
    for (int p = 0; p < KHDAYS_UI_PARTS; ++p) {
        at += (size_t)snprintf(line + at, sizeof(line) - at, "%s%s %s", p ? ", " : "", names[p], mode_names[modes[p]]);
    }
    khdays_log("ui: the port's presentation: %s", line);
    if (khdays_ui_any()) {
        fprintf(stderr, "ui: %s\n", line);
    }
}

int khdays_ui_mode(int part)
{
    if (!ready) {
        khdays_ui_parts_init();
    }
    return part >= 0 && part < KHDAYS_UI_PARTS ? modes[part] : KHDAYS_UI_OFF;
}

const char *khdays_ui_part_name(int part)
{
    return part >= 0 && part < KHDAYS_UI_PARTS ? names[part] : "?";
}

int khdays_ui_any_checked(void)
{
    for (int p = 0; p < KHDAYS_UI_PARTS; ++p) {
        if (khdays_ui_mode(p) == KHDAYS_UI_CHECK) {
            return 1;
        }
    }
    return 0;
}

int khdays_ui_any(void)
{
    for (int p = 0; p < KHDAYS_UI_PARTS; ++p) {
        if (khdays_ui_mode(p) != KHDAYS_UI_OFF) {
            return 1;
        }
    }
    return 0;
}
