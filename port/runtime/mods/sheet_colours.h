/* The colours the game draws each 2D sheet's tiles with, for the studio's sheets whose layout the game
 * builds at run time (the field HUD's): while the graphics dump is on, each tile's palette row and that
 * row's colours go to "<dump>/<name>.colours", adding up over plays (the format: docs/RENDERING.md,
 * "Mods' graphics"). */
#ifndef KHDAYS_SHEET_COLOURS_H
#define KHDAYS_SHEET_COLOURS_H

#include "../../hal/hal.h"

/* A VBlank (the game thread): while the dump is on, looks at what the screens
 * show now and then, and writes what changed every few seconds. */
void khdays_sheet_colours_frame(void);

#endif
