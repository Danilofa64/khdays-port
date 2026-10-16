/* The new game's dialogs as the port shows them (newgame.c), in Dream Drop Distance HD's style
 * (tools/newgame_dialog.py): the game's difficulty and its review, and between them the port's
 * question, whether the movies' subtitles show. */
#ifndef KHDAYS_NEWGAME_H
#define KHDAYS_NEWGAME_H

#include "../../hal/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { KHDAYS_NEWGAME_NONE, KHDAYS_NEWGAME_DIFFICULTY, KHDAYS_NEWGAME_SUBTITLES, KHDAYS_NEWGAME_REVIEW };

typedef struct KhdaysNewGame {
    int step;       /* KHDAYS_NEWGAME_* */
    int difficulty; /* 0-2: beginner, standard, proud */
    int button;     /* the one selected: the difficulty's row; the question's and the review's, 0 yes */
    int subtitles;  /* the movies' subtitles show */
} KhdaysNewGame;

/* The game's thread: the dialogs' state now. */
void khdays_newgame_state(KhdaysNewGame *out);

/* The game's thread, each game frame (input.c): the buttons the game gets; while the port asks its
 * question they are the port's (its B given to the game). */
u16 khdays_newgame_buttons(u16 buttons);

/* Whether the movies' subtitles show: the setting ([video] subtitles), and the window's thread's
 * way to keep an answer the question gave (1 once, with it in *on). */
void khdays_subtitles_set(int on);
int khdays_subtitles_shown(void);
int khdays_newgame_take_answer(int *on);

#ifdef __cplusplus
}
#endif

#endif
