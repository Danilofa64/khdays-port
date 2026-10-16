/* The colours the game draws each 2D sheet's tiles with, for the studio's sheets whose layout the game
 * builds at run time (the field HUD's). While the graphics dump is on, the palette row each tile of a
 * named sheet is drawn with, and that row's colours, go to "<dump>/<name>.colours":
 *
 *   row <r> <colours, BGR555 in hex>   16, or 256 for 8-bit tiles
 *   <first>-<last> <r>                 these tiles most often drawn with row r's colours
 *
 * Colours are kept per tile (a row holds other colours for other tiles); other colours a row had get
 * a row of their own from 17 on. What the file holds already is kept where this session saw nothing,
 * so plays add up. */
#ifndef KHDAYS_SHEET_COLOURS_H
#define KHDAYS_SHEET_COLOURS_H

#include "../../hal/hal.h"

/* A VBlank (the game thread): while the dump is on, looks at what the screens
 * show now and then, and writes what changed every few seconds. */
void khdays_sheet_colours_frame(void);

#endif
