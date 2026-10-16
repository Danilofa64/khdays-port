/* Where the parts' elements go. By
 * default an element stays at its DS place; a part's layout file,
 * mods/<Mod>/layouts/<part>.txt (found through the mods' order), can move
 * it. */
#ifndef KHDAYS_UI_LAYOUT_H
#define KHDAYS_UI_LAYOUT_H

#include <stdint.h>

/* Places element `id` of engine `*screen` from its DS place (x, y, w, h) and slot: where it goes, its
 * order in its slot and the engine it is drawn on; 1 when a layout line placed it. In widescreen an
 * element no line places goes with its group of touching elements (`group`, DS columns; NULL: alone)
 * to the left or right edge when the group lies in that half, and stays when it spans the middle. */
int khdays_ui_layout_place(const char *id, int *screen, int upper, int wide_margin, const float *group, float *x,
                           float *y, float *w, float *h, uint32_t *slot, int *order);

/* Whether a layout line places element `id` this frame (after khdays_ui_layout_frame). */
int khdays_ui_layout_names(const char *id);

/* Sets this frame's element ids, for the sections that apply while an
 * element is there (`[when <id>]`); kept until the next call. */
void khdays_ui_layout_frame(const char *const *ids, int count);

/* Reads a layout file's text as a mod's would be read (for the tests): its
 * entries apply over the DS places, and each line it cannot use is reported
 * once, naming `file`. */
void khdays_ui_layout_read(const char *file, const char *text);

#endif
