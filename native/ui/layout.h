/* Where the parts' elements go. By
 * default an element stays at its DS place; a part's layout file,
 * mods/<Mod>/layouts/<part>.txt (found through the mods' order), can move
 * it. */
#ifndef KHDAYS_UI_LAYOUT_H
#define KHDAYS_UI_LAYOUT_H

#include <stdint.h>

/* Places element `id` of engine `*screen`. It takes the element's DS place
 * (x, y, w, h, in DS pixels) and slot, and gives back where it goes, its
 * order among its slot's elements and the engine it is drawn on (`upper` is
 * the engine on the upper screen). Returns 1 when a layout line placed it.
 * In widescreen (`wide_margin` DS columns beside the DS's on each side of
 * engine A), an element no line places goes to the left edge when it lies
 * wholly in the left half, to the right edge when wholly in the right half,
 * and stays when it is across the middle (the same rule as for the game's
 * pieces). */
int khdays_ui_layout_place(const char *id, int *screen, int upper, int wide_margin, float *x, float *y, float *w,
                           float *h, uint32_t *slot, int *order);

/* Sets this frame's element ids, for the sections that apply while an
 * element is there (`[when <id>]`); kept until the next call. */
void khdays_ui_layout_frame(const char *const *ids, int count);

/* Reads a layout file's text as a mod's would be read (for the tests): its
 * entries apply over the DS places, and each line it cannot use is reported
 * once, naming `file`. */
void khdays_ui_layout_read(const char *file, const char *text);

/* Forgets every entry (for the tests). */
void khdays_ui_layout_reset(void);

#endif
