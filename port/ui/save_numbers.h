/* The file numbers on the rows of the game's own save lists while they page through the port's files
 * (save_numbers.cpp; runtime/menus/savelist.c "The game's own lists in pages"). */
#ifndef KHDAYS_UI_SAVE_NUMBERS_H
#define KHDAYS_UI_SAVE_NUMBERS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Gives each shown row's file number in place of the game's own "01" to "03" (at the HUD part's
 * VBlank, as the badges and the crowns). */
void khdays_ui_save_numbers_emit(void);

#ifdef __cplusplus
}
#endif

#endif
