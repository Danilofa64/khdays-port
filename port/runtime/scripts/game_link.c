/* The game's side of the mods' interface (game_link.h), by the decompilation's names. */
#include "game_link.h"

#include "../core/language.h"
#include "../core/session_log.h"
#include "../core/window.h"
#include "../core/events.h"
#include "../../host/app/instance.h"
#include "../menus/autosave.h"
#include "../menus/camp.h"
#include "../menus/mission_start.h"
#include "../net/net_session.h"
#include "../sound/sound_bridge.h"
#include "khdays_ds_symbols.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern int GameState_IsFlagSet(int flag);
extern int GameState_SetFlag(int flag);
extern void GameState_ClearFlag(int flag);
extern unsigned int Session_GetLocalPlayerIndex(void);
extern int GetEntryField20ByIndex(int idx);
extern void func_ov022_02088218(int player, KhdaysRef at);
extern int PlaySound(int sequence_archive, int entry);
extern KhdaysRef NNSi_FndAllocFromDefaultExpHeap(unsigned int size);
extern void NNSi_FndFreeFromDefaultHeap(KhdaysRef block);
extern void Ov002_ShowNoticeText(KhdaysRef text);
extern void Ov022_ActorSetHp(KhdaysRef pEnt, int nValue); /* the game's pointers are references (window.h) */
extern void Ov002_PauseMissionScene(void);
extern int GetLanguage(void);
extern void Ov000_TickMenuLoop(void);
extern void GameState_SetField(unsigned int field, unsigned int bits, unsigned int value);
int khdays_overlay_loaded(int id); /* runtime/core/overlays.c */

#define TITLE_SCENE 1u
#define FIELD_SCENE 2u
#define STORY_CAMP_SCENE 5u
#define MISSION_CAMP_SCENE 19u
#define OV_BATTLE 22
#define OV_TITLE 0
#define OBJECT_UPDATE 0x14u /* an object's update (its next state) */
#define OV_FIELD 2
#define OV_MISSION_CAMP 8
#define OV_STORY_CAMP 25
#define CAPTIONS 0x0207f62cu /* data_ov002_0207f62c: [1] the field's captions (Ov002_ShowNoticeText) */
#define CAPTION_LINE 0xb8u   /* the notice held, 0 while none */
#define OBJ_FRAME 0x8u       /* gObjSystem: [2] the game's frame count */
#define SCENE_CURRENT 0x8u   /* gSceneCtl: current id, pending +0xc, argument +0x10 */
#define SCENE_ARGUMENT 0x10u
#define STATE_MUNNY 0xcu      /* GameState: nMunny (Ov008_CanConfirmShopRow) */
#define STATE_ITEMS 0x810u    /* GameState: aItemCount */
#define FLAG_ITEM_OWNED 0x4db /* Ov008_AddItemCount */
#define PLAYERS 0x020b2e7cu   /* *: the field's players, 0xc bytes each; +4 a record whose +0x20 is the actor */
#define BATTLE 0x020b2e60u    /* data_ov022_020b2e60: the battle's context, set as it begins (Ov022_BeginScene) */
#define ACTOR_POSITION 0x48cu
#define ENT_OWNER 0x8u /* Ov022_ActorSetHp's entity: nOwner, nId +9, nHp +0x12, nHpMax +0x16 */
#define ENT_ID 0x9u
#define ENT_HP 0x12u
#define ENT_HP_MAX 0x16u
#define OV_GAME_OVER 27
#define FIELD_ROOT 0x0207fa00u /* data_ov002_0207fa00: the field's root */
#define ROOT_STOPPING 0x8b64u  /* bits 0-1: the field stopping or stopped (Ov002_PauseMissionScene) */
#define GIVE_UP_FIELD 0x20efu  /* the game field the pause's Give Up sets as it confirms */
#define ENEMY_KIND 0x19cu
#define QUIT_FLAG 0x2087 /* set by Ov002_RetireSubFlow on giving up (runtime/menus/retry.c) */

static unsigned rd32(KhdaysRef at)
{
    return *(const volatile unsigned *)KHDAYS_W(at);
}

void khdays_link_folders(const char **mods, const char **user)
{
    *mods = "mods";
    *user = khdays_host_user_folder();
}

unsigned khdays_link_frame(void)
{
    return rd32(KHDAYS_DS_gObjSystem + OBJ_FRAME);
}

int khdays_link_paused(void)
{
    /* main's loop: mode 1 runs the callbacks only, the objects stand still */
    return *(const volatile unsigned char *)KHDAYS_W(KHDAYS_DS_gPauseMode) == 1;
}

unsigned khdays_link_scene(unsigned *argument)
{
    if (argument != NULL) {
        *argument = rd32(KHDAYS_DS_gSceneCtl + SCENE_ARGUMENT);
    }
    return rd32(KHDAYS_DS_gSceneCtl + SCENE_CURRENT);
}

int khdays_link_where(void)
{
    const unsigned scene = khdays_link_scene(NULL);
    if (scene == FIELD_SCENE && khdays_overlay_loaded(OV_FIELD) && khdays_overlay_loaded(OV_BATTLE)) {
        return KHDAYS_WHERE_FIELD;
    }
    if ((scene == MISSION_CAMP_SCENE && khdays_overlay_loaded(OV_MISSION_CAMP)) ||
        (scene == STORY_CAMP_SCENE && khdays_overlay_loaded(OV_STORY_CAMP))) {
        return KHDAYS_WHERE_CAMP;
    }
    return scene == TITLE_SCENE ? KHDAYS_WHERE_TITLE : KHDAYS_WHERE_ELSEWHERE;
}

unsigned khdays_link_state(void)
{
    const unsigned state = rd32(KHDAYS_DS_gGameState);
    return khdays_game_ram(state) ? state : 0;
}

int khdays_link_munny(void)
{
    const unsigned state = khdays_link_state();
    return state != 0 ? (int)rd32(state + STATE_MUNNY) : 0;
}

void khdays_link_set_munny(int value)
{
    const unsigned state = khdays_link_state();
    if (state != 0) {
        *(volatile unsigned *)KHDAYS_W(state + STATE_MUNNY) = (unsigned)value;
    }
}

int khdays_link_items(unsigned char *counts)
{
    const unsigned state = khdays_link_state();
    if (state == 0) {
        return 0;
    }
    for (int i = 0; i < KHDAYS_LINK_ITEMS; ++i) {
        counts[i] = *(const volatile unsigned char *)KHDAYS_W(state + STATE_ITEMS + (unsigned)i);
    }
    return 1;
}

void khdays_link_set_item(int id, int count)
{
    const unsigned state = khdays_link_state();
    if (state == 0) {
        return;
    }
    *(volatile unsigned char *)KHDAYS_W(state + STATE_ITEMS + (unsigned)id) = (unsigned char)count;
    if (count > 0) {
        GameState_SetFlag(FLAG_ITEM_OWNED + id);
    }
}

int khdays_link_flag(int n)
{
    return GameState_IsFlagSet(n) != 0;
}

void khdays_link_set_flag(int n, int on)
{
    if (on) {
        GameState_SetFlag(n);
    } else {
        GameState_ClearFlag(n);
    }
}

int khdays_link_mission(void)
{
    if (khdays_link_scene(NULL) != FIELD_SCENE) {
        return -1;
    }
    return *(const volatile unsigned short *)KHDAYS_W(KHDAYS_DS_data_0204c23c);
}

/* The local player's battle actor, 0 without one. */
static unsigned player_actor(void)
{
    unsigned actor;
    if (!khdays_overlay_loaded(OV_BATTLE) || !khdays_game_ram(rd32(PLAYERS))) {
        return 0;
    }
    actor = (unsigned)GetEntryField20ByIndex((int)Session_GetLocalPlayerIndex());
    return khdays_game_ram(actor) ? actor : 0;
}

int khdays_link_position(float at[3])
{
    const unsigned actor = player_actor();
    if (actor == 0) {
        return 0;
    }
    for (int i = 0; i < 3; ++i) {
        at[i] = (float)(int)rd32(actor + ACTOR_POSITION + 4u * (unsigned)i) / 4096.0f;
    }
    return 1;
}

const char *khdays_link_set_position(const float at[3])
{
    KhdaysRef vec;
    if (player_actor() == 0) {
        return "no player";
    }
    if (rd32(BATTLE) == 0) {
        return "not now"; /* the routine reads the battle's frame time: the battle has not begun */
    }
    vec = NNSi_FndAllocFromDefaultExpHeap(12);
    if (vec == 0) {
        return "not enough memory";
    }
    for (int i = 0; i < 3; ++i) {
        const double v = (double)at[i] * 4096.0;
        *(volatile int *)KHDAYS_W(vec + 4u * (unsigned)i) = (int)(v < -2147483647.0  ? -2147483647.0
                                                                  : v > 2147483647.0 ? 2147483647.0
                                                                                     : v);
    }
    func_ov022_02088218((int)Session_GetLocalPlayerIndex(), vec);
    NNSi_FndFreeFromDefaultHeap(vec);
    return NULL;
}

int khdays_link_player_hp(int *hp, int *max)
{
    const unsigned actor = player_actor();
    if (actor == 0) {
        return 0;
    }
    khdays_link_entity_hp(KHDAYS_W(actor), hp, max);
    return 1;
}

int khdays_link_set_player_hp(int hp)
{
    const unsigned actor = player_actor();
    if (actor == 0) {
        return 0;
    }
    Ov022_ActorSetHp(actor, hp);
    return 1;
}

void khdays_link_entity_hp(const void *entity, int *hp, int *max)
{
    const unsigned char *e = entity;
    *hp = *(const unsigned short *)(e + ENT_HP);
    *max = *(const unsigned short *)(e + ENT_HP_MAX);
}

int khdays_link_is_player(const void *entity)
{
    const unsigned actor = player_actor();
    return actor != 0 && KHDAYS_REF(entity) == actor;
}

static unsigned hit_source; /* the hit being dealt's source, 0 between hits */

void khdays_link_hit_from(void *source)
{
    hit_source = source != NULL ? KHDAYS_REF(source) : 0;
}

int khdays_link_attacker(unsigned *actor)
{
    *actor = 0;
    /* an enemy is an ov107 actor: its first halfword's 0x40 files it among a room's enemies
     * (Ov107_Region_AddMember), +0x19c its kind (Ov107_Spawner_FindFreeActor) */
    if (hit_source == 0 || !khdays_game_ram(hit_source) ||
        !(*(const volatile unsigned short *)KHDAYS_W(hit_source) & 0x40u)) {
        return -1;
    }
    *actor = hit_source;
    return *(const volatile unsigned char *)KHDAYS_W(hit_source + ENEMY_KIND);
}

int khdays_link_language(void)
{
    return khdays_language_wanted() != 0 ? khdays_language_wanted() : GetLanguage();
}

int khdays_link_title_idle(void)
{
    const unsigned object = rd32(KHDAYS_DS_gSceneCtl); /* the scene's task */
    return khdays_link_scene(NULL) == TITLE_SCENE && khdays_overlay_loaded(OV_TITLE) && khdays_game_ram(object) &&
           khdays_function_lookup(rd32(object + OBJECT_UPDATE)) == (khdays_function_pointer)Ov000_TickMenuLoop;
}

int khdays_link_game_over(void)
{
    return khdays_overlay_loaded(OV_GAME_OVER);
}

int khdays_link_results_cleared(void)
{
    return khdays_autosave_results_cleared();
}

const char *khdays_link_start_mission(int number)
{
    return khdays_mission_start_request(number);
}

const char *khdays_link_leave_mission(void)
{
    const unsigned root = rd32(FIELD_ROOT);
    if (khdays_link_scene(NULL) != FIELD_SCENE || !khdays_overlay_loaded(OV_FIELD) || !khdays_game_ram(root)) {
        return "not in the field";
    }
    if (khdays_link_where() != KHDAYS_WHERE_FIELD) {
        return "not now"; /* the field's opening, before its battle is loaded */
    }
    if (khdays_link_paused() || khdays_link_menu_open(KHDAYS_LINK_PAUSE) || (rd32(root + ROOT_STOPPING) & 3u)) {
        return "not now";
    }
    Ov002_PauseMissionScene();
    if (!(rd32(root + ROOT_STOPPING) & 1u)) {
        return "not now"; /* the field is held by something else (an event, a talk) */
    }
    GameState_SetField(GIVE_UP_FIELD, 1, 0);
    return NULL;
}

static int in_field(void)
{
    return khdays_link_scene(NULL) == FIELD_SCENE && khdays_overlay_loaded(OV_FIELD);
}

const char *khdays_link_play_sound(const char *sequence_archive, const char *entry)
{
    const KhdaysSoundArchive *archive = khdays_sound_bridge_archive();
    int number, index;
    if (archive == NULL || !khdays_sound_archive_find_entry(archive, sequence_archive, entry, &number, &index)) {
        return "unknown sound";
    }
    PlaySound(number, index);
    return NULL;
}

const char *khdays_link_show_text(const unsigned short *text, int length)
{
    unsigned captions;
    KhdaysRef block;
    if (!in_field()) {
        return "not in the field";
    }
    captions = rd32(CAPTIONS + 4u);
    if (!khdays_game_ram(captions)) {
        return "not in the field";
    }
    if (rd32(captions + CAPTION_LINE) != 0) {
        return "busy";
    }
    block = NNSi_FndAllocFromDefaultExpHeap((unsigned)(length + 1) * 2u);
    if (!khdays_game_ram(block)) {
        return "no room in the game's heap";
    }
    for (int i = 0; i < length; ++i) {
        ((volatile unsigned short *)KHDAYS_W(block))[i] = text[i];
    }
    ((volatile unsigned short *)KHDAYS_W(block))[length] = 0;
    Ov002_ShowNoticeText(block); /* it keeps a copy of its own */
    NNSi_FndFreeFromDefaultHeap(block);
    return NULL;
}

int khdays_link_menu_open(int menu)
{
    if (menu == KHDAYS_LINK_PAUSE) {
        return khdays_link_scene(NULL) == FIELD_SCENE &&
               *(const volatile unsigned char *)KHDAYS_W(KHDAYS_DS_gPauseMode) != 0;
    }
    {
        int story;
        return khdays_camp_screen(&story) != KHDAYS_CAMP_NONE;
    }
}

int khdays_link_guest(void)
{
    /* the game's own test for the host: player 0 (Ov022_ActorSetHp, Ov022_ApplyDamageAndFlagHit) */
    return Session_GetLocalPlayerIndex() != 0;
}

void khdays_link_trace_frame(void)
{
    static unsigned scene = ~0u, points, vblanks_at;
    static int game_over;
    const unsigned now = khdays_link_scene(NULL), vblanks = khdays_events_vblank_count();
    const int over = khdays_overlay_loaded(OV_GAME_OVER);
    ++points;
    if (now != scene) {
        if (scene != ~0u) {
            fprintf(stderr, "mods-trace: scene %u: %u frame points over %u vblanks\n", scene, points,
                    vblanks - vblanks_at);
        }
        scene = now;
        points = 0;
        vblanks_at = vblanks;
    }
    if (over && !game_over && khdays_link_state() != 0) {
        fprintf(stderr, "mods-trace: game over: flag 0x2087 %d, mission %u\n", GameState_IsFlagSet(QUIT_FLAG) != 0,
                *(const volatile unsigned short *)KHDAYS_W(KHDAYS_DS_data_0204c23c));
    }
    game_over = over;
}

void khdays_link_trace_hp(const void *entity, int before)
{
    const unsigned char *e = entity;
    int hp, max;
    khdays_link_entity_hp(entity, &hp, &max);
    fprintf(stderr,
            "mods-trace: hp: entity %08x owner %u id %u flags %08x%08x, %d -> %d of %d; local player %u, its actor "
            "%08x\n",
            KHDAYS_REF(entity), e[ENT_OWNER], e[ENT_ID], *(const unsigned *)(e + 4), *(const unsigned *)e, before, hp,
            max, Session_GetLocalPlayerIndex(), player_actor());
}

/* ---- The deck ---------------------------------------------------------------------------------- */

#define STATE_DECK 0xee0u /* GameState: the deck's cells, an item id each */
#define DB_ROW 0x14u      /* data_0204c238's rows (MsgDb_InitTable): +8 the records, +0xc their count */
#define DB_RECORDS 0x8u
#define DB_COUNT 0xcu
#define DB_PANELS 0x15      /* the items' records, from id 1 (MsgDb_LoadRecord15) */
#define DB_SHAPES 0x1b      /* the panels' shapes, a 64-bit cell mask each (MsgDb_FetchEntryPair0x1b) */
#define DB_SLOT 0xe         /* where the game loads them (Ov008_BuildMenuGrid) */
#define PANEL_BYTES 0x64u   /* +0 category, +6 kind, +8 shape (signed), +9 / +0xa the panel's cell in it */
#define CAMP_MENU 0         /* khdays_camp_screen's menu */
#define CATEGORY_MATERIAL 2 /* the synthesis materials, recipes and techs: no panels of the deck */

extern int MsgDb_IsLoaded(int id);
extern int MsgDb_LoadDb(int id, int slot);
extern int ResSlot_Release_2(int id);
extern int Ov008_IsGridItemEligible(int itemId);

static KhdaysLinkPanel *panels; /* by id - 1 */
static int panel_count, panels_read;

static unsigned rd16(KhdaysRef at)
{
    return *(const volatile unsigned short *)KHDAYS_W(at);
}

static unsigned rd8(KhdaysRef at)
{
    return *(const volatile unsigned char *)KHDAYS_W(at);
}

/* KHDAYS_TRACE_DECK: each panel's record as read, and the deck then, on stderr. */
static void trace_panels(void)
{
    unsigned short cells[KHDAYS_LINK_DECK_CELLS];
    for (int i = 0; i < panel_count; ++i) {
        const KhdaysLinkPanel *p = &panels[i];
        fprintf(stderr, "deck-trace: panel %d: category %d kind %d shape %d at %d,%d cells %016llx\n", i + 1,
                p->category, p->kind, p->shape, p->col, p->row, p->cells);
    }
    if (khdays_link_deck(cells)) {
        for (int c = 0; c < KHDAYS_LINK_DECK_CELLS; ++c) {
            if (cells[c] != 0) {
                fprintf(stderr, "deck-trace: cell %d (page %d row %d col %d): %u\n", c, c / KHDAYS_LINK_DECK_PAGE,
                        c % KHDAYS_LINK_DECK_PAGE / KHDAYS_LINK_DECK_COLS, c % KHDAYS_LINK_DECK_COLS, cells[c]);
            }
        }
    }
}

/* The panels' records, read once from the game's databases (loaded for it when they are not). */
static void read_panels(void)
{
    const unsigned table = rd32(KHDAYS_DS_data_0204c238);
    const int dbs[2] = {DB_PANELS, DB_SHAPES};
    int loaded[2] = {0, 0};
    if (table == 0 || khdays_link_state() == 0) {
        return; /* the game is not up yet: another time */
    }
    panels_read = 1;
    for (int d = 0; d < 2; ++d) {
        if (!MsgDb_IsLoaded(dbs[d])) {
            MsgDb_LoadDb(dbs[d], DB_SLOT);
            loaded[d] = 1;
        }
    }
    if (MsgDb_IsLoaded(DB_PANELS) && MsgDb_IsLoaded(DB_SHAPES)) {
        const unsigned records = rd32(table + DB_PANELS * DB_ROW + DB_RECORDS);
        const unsigned shapes = rd32(table + DB_SHAPES * DB_ROW + DB_RECORDS);
        const int shape_count = (int)rd16(table + DB_SHAPES * DB_ROW + DB_COUNT);
        const int count = (int)rd16(table + DB_PANELS * DB_ROW + DB_COUNT);
        panels = count > 0 ? calloc((size_t)count, sizeof(*panels)) : NULL;
        panel_count = panels != NULL ? count : 0;
        for (int i = 0; i < panel_count; ++i) {
            const unsigned r = records + (unsigned)i * PANEL_BYTES;
            KhdaysLinkPanel *p = &panels[i];
            p->category = (int)rd16(r);
            p->kind = (short)rd16(r + 6u);
            p->shape = (signed char)rd8(r + 8u);
            p->col = (int)rd8(r + 9u);
            p->row = (int)rd8(r + 0xau);
            if (p->shape >= shape_count) {
                p->shape = -1;
            }
            if (p->shape >= 0) {
                const unsigned at = shapes + (unsigned)p->shape * 8u;
                p->cells = (unsigned long long)rd32(at) | (unsigned long long)rd32(at + 4u) << 32;
            }
        }
    }
    for (int d = 1; d >= 0; --d) {
        if (loaded[d]) {
            ResSlot_Release_2(dbs[d]);
        }
    }
    khdays_log("mods: %d panels' records read", panel_count);
    if (getenv("KHDAYS_TRACE_DECK") != NULL) {
        trace_panels();
    }
}

int khdays_link_panel(int id, KhdaysLinkPanel *out)
{
    if (!panels_read) {
        read_panels();
    }
    if (id < 1 || id > panel_count || !Ov008_IsGridItemEligible(id) || panels[id - 1].category == CATEGORY_MATERIAL) {
        return 0;
    }
    *out = panels[id - 1];
    return 1;
}

int khdays_link_deck(unsigned short *cells)
{
    const unsigned state = khdays_link_state();
    if (state == 0) {
        return 0;
    }
    for (int c = 0; c < KHDAYS_LINK_DECK_CELLS; ++c) {
        cells[c] = (unsigned short)rd16(state + STATE_DECK + 2u * (unsigned)c);
    }
    return 1;
}

const char *khdays_link_deck_busy(void)
{
    int story;
    const int screen = khdays_camp_screen(&story);
    return screen == KHDAYS_CAMP_NONE || screen == CAMP_MENU ? NULL : "not now";
}

void khdays_link_set_deck(const unsigned short *cells)
{
    const unsigned state = khdays_link_state();
    for (int c = 0; state != 0 && c < KHDAYS_LINK_DECK_CELLS; ++c) {
        *(volatile unsigned short *)KHDAYS_W(state + STATE_DECK + 2u * (unsigned)c) = cells[c];
    }
}

/* ---- Time, music, the seed, sharing ------------------------------------------------------------ */

#define MUSIC_TRACK 0xb46f6u /* gSoundMgr: the music player's track (a sequence), -1 none (SoundMgr_Update) */
#define MUSIC_STOP 3         /* the sound manager's request kinds (ScriptQueue_Push's callers) */
#define MUSIC_SWITCH 4
#define MUSIC_RESUME 5

extern int SoundMgr_SwitchBgm(int seq, int fade);
extern int SoundMgr_SwitchBgmResume(int seq, int fade);
extern int SoundMgr_QueueKind1(int arg);
extern int SetSelectionIfChanged(int param_1);
extern void RequestQueue_SetOrPushKind3(int arg);

static struct {
    int holding;    /* a mod's track plays: the game's requests are kept back */
    int letting;    /* a request of the port's goes through */
    unsigned scene; /* the scene it plays in */
    int kept;       /* the game's last request kept back: its kind, track and value */
    int kind, arg, value;
    int before; /* the track playing as the mod's took its place, -1 none */
} music;

static unsigned long long own_seed;

unsigned khdays_link_vblanks(void)
{
    return khdays_events_vblank_count();
}

int khdays_link_music_track(const char *name)
{
    const KhdaysSoundArchive *archive = khdays_sound_bridge_archive();
    const int count = archive != NULL ? khdays_sound_archive_count(archive, KHDAYS_SOUND_SEQUENCE) : 0;
    for (int n = 0; n < count; ++n) {
        char id[96];
        if (khdays_sound_archive_identifier(archive, KHDAYS_SOUND_SEQUENCE, n, 0, id, (int)sizeof(id)) &&
            (strcmp(id, name) == 0 || (strncmp(id, "seq/", 4) == 0 && strcmp(id + 4, name) == 0))) {
            return n;
        }
    }
    return -1;
}

int khdays_link_music_request(int kind, int arg, int value)
{
    if (!music.holding || music.letting) {
        return 0;
    }
    music.kept = 1;
    music.kind = kind;
    music.arg = arg;
    music.value = value;
    return 1;
}

const char *khdays_link_music_play(int track, int fade)
{
    const unsigned manager = rd32(KHDAYS_DS_gSoundMgr);
    if (!khdays_game_ram(manager)) {
        return "no game";
    }
    if (!music.holding) {
        music.before = (short)rd16(manager + MUSIC_TRACK);
        music.kept = 0;
        music.holding = 1;
    }
    music.scene = khdays_link_scene(NULL);
    music.letting = 1;
    SoundMgr_SwitchBgm(track, fade);
    music.letting = 0;
    return NULL;
}

void khdays_link_music_stop(int fade)
{
    if (!music.holding) {
        return;
    }
    music.holding = 0;
    if (!music.kept) {
        if (music.before >= 0) {
            SoundMgr_SwitchBgm(music.before, fade);
        } else {
            RequestQueue_SetOrPushKind3(fade);
        }
        return;
    }
    switch (music.kind) {
    case 1: SoundMgr_QueueKind1(music.arg); break;
    case 2: SetSelectionIfChanged(music.arg); break;
    case MUSIC_STOP: RequestQueue_SetOrPushKind3(music.value); break;
    case MUSIC_RESUME: SoundMgr_SwitchBgmResume(music.arg, music.value); break;
    default: SoundMgr_SwitchBgm(music.arg, music.value); break;
    }
}

int khdays_link_music_playing(void)
{
    return music.holding;
}

void khdays_link_music_frame(void)
{
    if (music.holding && khdays_link_scene(NULL) != music.scene) {
        khdays_link_music_stop(0);
    }
}

static unsigned long long fresh_seed(void)
{
    unsigned long long seed = SDL_GetPerformanceCounter() ^ (unsigned long long)time(NULL) << 32;
    return seed != 0 ? seed : 1;
}

unsigned long long khdays_link_seed(int *in_room)
{
    unsigned long long seed;
    *in_room = khdays_net_seed(&seed);
    if (*in_room) {
        return seed;
    }
    if (khdays_net_in_session()) {
        return 0; /* a guest, its host's seed not come yet */
    }
    if (own_seed == 0) {
        own_seed = fresh_seed();
    }
    return own_seed;
}

void khdays_link_next_seed(void)
{
    own_seed = fresh_seed();
    khdays_net_set_seed(fresh_seed()); /* the host's room; nothing on a guest or alone */
}

int khdays_link_share(const void *data, unsigned size)
{
    return khdays_net_share(data, size);
}

int khdays_link_shared(unsigned char **data, unsigned *size)
{
    return khdays_net_shared(data, size);
}
