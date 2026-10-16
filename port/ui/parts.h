/* The parts of the game's presentation the port draws itself.
 * KHDAYS_UI switches each one: off (the game's own
 * drawing), check (both, compared in the same frame) or on (the port's
 * drawing). */
#ifndef KHDAYS_UI_PARTS_H
#define KHDAYS_UI_PARTS_H

enum {
    KHDAYS_UI_TEXT,
    KHDAYS_UI_HUD,
    KHDAYS_UI_PAUSE,
    KHDAYS_UI_CAMP,
    KHDAYS_UI_SAVE,
    KHDAYS_UI_MISSION,
    KHDAYS_UI_SHOP,
    KHDAYS_UI_PARTS
};

enum { KHDAYS_UI_OFF, KHDAYS_UI_CHECK, KHDAYS_UI_ON };

/* Reads KHDAYS_UI=<part>=<off|check|on>[,<part>=<mode>...] once (all=<mode>
 * sets every part), and writes each part's mode in the session log. */
void khdays_ui_parts_init(void);

/* A part's mode (KHDAYS_UI_OFF, _CHECK or _ON), which the game's hooks ask
 * for (port/tools/prepare.py). */
int khdays_ui_mode(int part);

/* A part's name, as KHDAYS_UI and the elements' ids spell it. */
const char *khdays_ui_part_name(int part);

/* Whether any part is checked, or drawn by the port. */
int khdays_ui_any_checked(void);
int khdays_ui_any(void);

#endif
