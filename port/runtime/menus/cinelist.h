/* MODO CINE's list (cinelist.c), drawn as the HD collection's theater draws its chapters
 * (host/screens/cine_screen.c, a mod's layouts/cine/<lang>/cine.txt). The game's own list runs
 * underneath (its buttons, pages, fades, the movie A starts); the port only reads and shows it. */
#ifndef KHDAYS_CINELIST_H
#define KHDAYS_CINELIST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the game's rows (Ov000_FillListRows) */
#define KHDAYS_CINE_SCENES 28

typedef struct KhdaysCineView {
    int shown;
    int language;                        /* the game's (1-5), the one the player just chose while it applies */
    int first;                           /* the first row of the page shown (Ov000_DrawListScene's ten) */
    int cursor;                          /* the scene selected */
    int16_t master;                      /* the game's fade: its brightness, -16 black to 16 white */
    uint16_t movies[KHDAYS_CINE_SCENES]; /* each row's movie (its number: <n>.z in UI/thr/m.p2) */
} KhdaysCineView;

/* The game's thread: an object's update ran (diag.c, its native address). */
void khdays_cinelist_update_ran(uintptr_t update);
/* The game's thread, once a frame (input.c): what the list is, published. */
void khdays_cinelist_frame(void);
/* Any thread: what the list is, as last published. */
void khdays_cinelist_view(KhdaysCineView *out);

#ifdef __cplusplus
}
#endif

#endif
