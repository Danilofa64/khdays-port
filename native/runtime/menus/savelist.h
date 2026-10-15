/* The port's save list (savelist.c): the game's list of save files drawn in the port's pieces
 * (host/screens/save_screen.c, a mod's layouts/save/<lang>/save.txt), with as many files as
 * saveslots.h keeps.
 *
 * The game's own list runs underneath, its pictures hidden: on the title (CONTINUE's and Mission
 * Mode's) and in the camps' save screens. The port keeps its own cursor and brings the file chosen
 * under the game's slot 1 (khdays_slots_show), telling the game's record what it holds; then the game
 * gets the button. Prompts, reading, saving, deleting and fades are the game's, shown in its own words. */
#ifndef KHDAYS_SAVELIST_H
#define KHDAYS_SAVELIST_H

#include "saveslots.h"

#include <stdint.h>

enum { KHDAYS_SAVELIST_NONE, KHDAYS_SAVELIST_LOAD, KHDAYS_SAVELIST_SAVE };

/* Whose words the screen shows: the title's list's (UI/load/lrd_&.s.z) or the camp's save
 * screen's (UI/cm/str/sav_&.s.z). */
enum { KHDAYS_WORDS_LOAD, KHDAYS_WORDS_SAVE };

/* What a file holds, for its row: as the game's record of a slot has it
 * (Ov000_RecordLoadResult). */
typedef struct KhdaysSaveRow {
    uint8_t state;      /* KHDAYS_SLOT_* */
    uint8_t crown;      /* the story's end seen: field 0x44e is 6 */
    uint8_t face;       /* field 0xc4b, rolled at each save (BeginSaveToSlot): the plate's face */
    uint8_t difficulty; /* field 0x40a: 0 beginner, 1 standard, 2 proud */
    uint16_t day;       /* field 0 (9 bits) */
    uint16_t level;     /* the panels' count plus one, as the game counts them */
    uint32_t munny;     /* the state's +8 */
    uint32_t seconds;   /* the play time: the state's +0 */
} KhdaysSaveRow;

/* The list as the game's thread last left it. */
typedef struct KhdaysSaveView {
    int screen;     /* KHDAYS_SAVELIST_* */
    int words;      /* KHDAYS_WORDS_*: the strings ask[] names */
    int language;   /* the game's, 1-5 (English, French, German, Italian, Spanish) */
    int guest;      /* the guest's row after the files (Mission Mode's list) */
    int cursor;     /* the row selected: a file, or KHDAYS_SLOT_COUNT the guest's */
    int chosen;     /* the file under the game's slot 1 */
    int help;       /* the list's help shown (the title's list's states 1 and 2, the save screen's slots) */
    int ask[2];     /* the game's words in the prompt: records of its strings (`words`), -1 none */
    int ask_file;   /* the file their %d names (1 on) */
    int buttons;    /* the prompt's buttons: 0 none, 2 yes and no */
    int yes;        /* the button selected: 1 yes, 0 no */
    int16_t master; /* the lower screen's master brightness (the game's fades): -16 black to 16 white */
    uint32_t rows_changed; /* counts each time the rows are made again */
    KhdaysSaveRow rows[KHDAYS_SLOT_COUNT];
} KhdaysSaveView;

/* The game's buttons this frame, at Pad_Sample (input.c): those the port's
 * list takes are its, the rest the game's. */
uint16_t khdays_savelist_buttons(uint16_t buttons);

/* The list for the window's thread: a copy. Its screen NONE when none shows. */
void khdays_savelist_view(KhdaysSaveView *out);

/* Whether the list's pieces are there for `language` (1-5): a mod's
 * layouts/save/<lang>/save.txt. */
int khdays_savelist_available(int language);

#endif
