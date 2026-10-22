/* The game's side of the mods' interface: every name the interface gives reads, changes or calls the
 * game through these, by the decompilation's names (game_link.c). The tests give their own. */
#ifndef KHDAYS_SCRIPTS_GAME_LINK_H
#define KHDAYS_SCRIPTS_GAME_LINK_H

#define KHDAYS_LINK_ITEMS 0x8d0  /* gGameState->aItemCount's entries */
#define KHDAYS_LINK_FLAGS 0x4000 /* flags from *gGameState + 0x10 up to aItemCount at +0x810 */
#define KHDAYS_LINK_MUNNY_MAX 999999
#define KHDAYS_LINK_GIVEN_UP 0x2087 /* set by Ov002_RetireSubFlow on giving up (runtime/menus/retry.c) */

enum KhdaysLinkMenu { KHDAYS_LINK_PAUSE, KHDAYS_LINK_CAMP };

/* Where the mods are and the player's saves folder (their data, the mods' log). */
void khdays_link_folders(const char **mods, const char **user);

/* The game's frame count (the object list's), and whether it is paused (gPauseMode). */
unsigned khdays_link_frame(void);
int khdays_link_paused(void);

/* The current scene (gSceneCtl) and its argument. */
unsigned khdays_link_scene(unsigned *argument);

/* Where the game is, for the names of version 2 that act only there (api_v2.h): the field (scene 2 with
 * the field's and the battle's overlays), a camp (Mission Mode's, scene 19 with ov008; the story's,
 * scene 5 with ov025), the title (scene 1). */
enum KhdaysWhere { KHDAYS_WHERE_ELSEWHERE, KHDAYS_WHERE_FIELD, KHDAYS_WHERE_CAMP, KHDAYS_WHERE_TITLE };
int khdays_link_where(void);

/* The game's state (gGameState's reference), 0 while there is none: the state names and the item
 * actions need it. */
unsigned khdays_link_state(void);

int khdays_link_munny(void);
void khdays_link_set_munny(int value);

/* The item counts (KHDAYS_LINK_ITEMS bytes into `counts`); 0 without the game's state. */
int khdays_link_items(unsigned char *counts);
/* An item's count set, with its owned flag when above 0 (Ov008_AddItemCount's rule). */
void khdays_link_set_item(int id, int count);

/* The deck (gGameState +0xee0, Ov008_LoadGridFromSave): 3 pages of 8 rows of 5 cells, each panel's
 * item id at its own cell and 0 elsewhere. Open are the first cells in page, row and column order:
 * item 1's copies + 15, at most all (Ov008_GridMenuInitStep). */
#define KHDAYS_LINK_DECK_COLS 5
#define KHDAYS_LINK_DECK_ROWS 8
#define KHDAYS_LINK_DECK_PAGE (KHDAYS_LINK_DECK_COLS * KHDAYS_LINK_DECK_ROWS)
#define KHDAYS_LINK_DECK_CELLS (3 * KHDAYS_LINK_DECK_PAGE)
#define KHDAYS_LINK_DECK_OPENER 1 /* the item whose copies open cells */
#define KHDAYS_LINK_DECK_OPEN 15  /* the cells open without it */

/* The deck's cells (KHDAYS_LINK_DECK_CELLS); 0 without the game's state. */
int khdays_link_deck(unsigned short *cells);
/* Whether the deck may change now: NULL, else why not (a camp's page keeps its own copy of the deck and
 * writes it back as it closes, Ov008_DestroyGridMenu). */
const char *khdays_link_deck_busy(void);
void khdays_link_set_deck(const unsigned short *cells);

/* A panel as the game's records have it (message database 0x15, MsgDb_LoadRecord15; its shape from
 * 0x1b, MsgDb_FetchEntryPair0x1b). */
typedef struct KhdaysLinkPanel {
    int category;
    int kind;
    int shape;                /* -1: none (a link panel, set on another panel's cells) */
    unsigned long long cells; /* the shape's: bit column + 8 x row */
    int col, row;             /* the panel's own cell in its shape */
} KhdaysLinkPanel;
/* Item `id`'s panel; 0 when it is none: the camp's Panels do not list it (Ov008_IsGridItemEligible: the
 * Slot Releaser, the badges ...), it is a synthesis material (category 2), or the records cannot be read. */
int khdays_link_panel(int id, KhdaysLinkPanel *out);

int khdays_link_flag(int n);
void khdays_link_set_flag(int n, int on);

/* The VBlanks counted since the game began (60 a second, the host's clock): the mods' timers' time. */
unsigned khdays_link_vblanks(void);

/* The music a mod plays, on the game's music player as the game's own (SoundMgr_SwitchBgm): `track` a
 * sequence of the sound archive, its number by its name ("TwilightR_F" or "seq/TwilightR_F"; -1 none).
 * While it plays, the game's own requests to the sound manager are kept back (prepare.py's joins call
 * khdays_link_music_request: 1 when it keeps one); stopping it makes the last of them, else plays the
 * track it took the place of; it stops by itself as the scene changes (khdays_link_music_frame). */
int khdays_link_music_track(const char *name);
const char *khdays_link_music_play(int track, int fade);
void khdays_link_music_stop(int fade);
int khdays_link_music_playing(void);
void khdays_link_music_frame(void);
int khdays_link_music_request(int kind, int arg, int value);

/* The seed of the mods' draws: in a room the room's (chosen as it opened, changed by the host as each
 * mission ends; a guest's from the host, net_session.h), else this copy's own. *in_room says which;
 * 0 on a guest whose host has not sent it yet. */
unsigned long long khdays_link_seed(int *in_room);
/* A mission ended: the host (or a copy alone) picks the next mission's seed. */
void khdays_link_next_seed(void);
/* The host: bytes the mods share, to the room's guests (any size); 0 when nobody is there. A guest: the
 * next message the host's mods shared, in a block the caller frees; 0 when none waits. */
int khdays_link_share(const void *data, unsigned size);
int khdays_link_shared(unsigned char **data, unsigned *size);

/* The current mission (data_0204c23c) while the field is the scene; -1 otherwise. */
int khdays_link_mission(void);

/* The local player's battle actor is GetEntryField20ByIndex(Session_GetLocalPlayerIndex()): the field
 * players' table (*(data_ov022_020b2e78 + 4), 0xc bytes an entry, +4 a record whose +0x20 is the actor),
 * read with the battle (ov022) loaded. Its position is at +0x48c (20.12 fixed point), its HP at +0x12
 * and maximum at +0x16 (Ov022_ActorSetHp's entity). Each returns 0 without the actor. */
int khdays_link_position(float at[3]);
int khdays_link_player_hp(int *hp, int *max);
/* The local player put at `at` (the field's units) by the game's own routine for it
 * (func_ov022_02088218: its actor moved with its child, its position +0x48c, its motion reset after the
 * move); NULL when done, else why not. */
const char *khdays_link_set_position(const float at[3]);
/* Through Ov022_ActorSetHp, as the game's own hits and cures do. */
int khdays_link_set_player_hp(int hp);

/* An entity Ov022_ActorSetHp was given: its HP and maximum (nHp +0x12, nHpMax +0x16). */
void khdays_link_entity_hp(const void *entity, int *hp, int *max);

/* Whether that entity is the local player's actor. */
int khdays_link_is_player(const void *entity);

/* The hit being dealt (Ov022_ReceiveHit's join): its source, NULL once dealt. Its attacker: the enemy's
 * actor into `*actor` and its kind returned; -1 when the hit is no enemy's or none is being dealt. */
void khdays_link_hit_from(void *source);
int khdays_link_attacker(unsigned *actor);

/* The game's language (1-5, the firmware's: English .. Spanish): the one just chosen, else the game's. */
int khdays_link_language(void);

/* Whether the title's menu waits for the player (its scene object's update Ov000_TickMenuLoop). */
int khdays_link_title_idle(void);

/* Whether the game over (ov027) is up. */
int khdays_link_game_over(void);

/* Whether the results being recorded record the mission cleared (Ov005_CommitMissionResults' rule). */
int khdays_link_results_cleared(void);

/* The mission ended as the pause's Give Up does (PauseMenu_ConfirmFrame's yes: Ov002_PauseMissionScene and
 * game field 0x20ef), on to the results and the camp; NULL when done, else why not. */
const char *khdays_link_leave_mission(void);

/* Mission file `number` (mi/mi/<number>) started from Mission Mode's camp, by the camp's own presses
 * (runtime/menus/mission_start.h); NULL when on its way, else why not. */
const char *khdays_link_start_mission(int number);

/* KHDAYS_TRACE_MODS: what the interface's open questions are answered from, on stderr: each HP
 * change's entity against the field's players, the game over's flags, the frame points per scene. */
void khdays_link_trace_frame(void);
void khdays_link_trace_hp(const void *entity, int before);

/* The game's actions, each through its own routine; NULL when done, else the reason it was not. */

/* PlaySound(archive, entry), the sequence archive's entry found by its names in the sound archive. */
const char *khdays_link_play_sound(const char *sequence_archive, const char *entry);

/* Ov002_ShowNoticeText with `text` (`length` UTF-16 units) in a block of the game's heap: the field's
 * notice, of which the game takes the first only ("busy" after it). */
const char *khdays_link_show_text(const unsigned short *text, int length);

/* Whether a menu is open: the pause, the camp. */
int khdays_link_menu_open(int menu);

/* A guest in a multiplayer session: the host alone changes the game. */
int khdays_link_guest(void);

#endif
