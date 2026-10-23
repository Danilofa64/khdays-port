/* The Stop magic, the game's side (stop_magic.h). Its three panels take three of the panels' table's blank records
 * and their words go into the table's strings as the database loads; the battle's list of magic names gets the
 * names the game left empty; in battle, its slot, casts and holds (below; the holds themselves in stop_hold.c).
 * docs/RUNTIME_MAP.md, "The magic". */
#include "stop_magic.h"

#include "stop_data.h"
#include "stop_hold.h"

#include "../core/session_log.h"
#include "../core/utf8.h"
#include "../core/window.h"
#include "../../hal/hal.h"
#include "../scripts/deck_link.h"
#include "../scripts/game_link.h"
#include "khdays_ds_symbols.h"

#include <stdlib.h>
#include <string.h>

extern const unsigned char khdays_stop_magic_text[];
extern const unsigned int khdays_stop_magic_text_size;

extern int GetLanguage(void); /* the game's: gLangPath's language, 1 en, 2 fr, 3 de, 4 it, 5 es */
extern KhdaysRef NNSi_FndAllocFromDefaultExpHeap(unsigned int size);
extern void NNSi_FndFreeFromDefaultHeap(KhdaysRef block);

#define DB_ROW 0x14u    /* data_0204c238's rows (MsgDb_LoadDb): +0 the records' pack, +4 the strings' */
#define DB_STRINGS 0x4u /* the strings' pack: a count, each string's end, then the strings (P2_ExtractString) */
#define DB_RECORDS 0x8u
#define DB_COUNT 0xcu
#define DB_PANELS 0x15

#define PANEL_BYTES 0x64u /* a panel's record (MsgDb_LoadRecord15) */
#define PANEL_CATEGORY 0x0u
#define PANEL_NUMBER 0x4u /* a number of its own (0 for the blank records) */
#define PANEL_KIND 0x6u   /* its parameter (-1 none) */
#define PANEL_MAGIC 0x18u /* a magic's index in the battle's tables, 1-18 */
#define PANEL_PRICE 0x58u
#define CATEGORY_MATERIAL 2 /* the blank records' */
#define BLIZZARD 96         /* Blizzard's first level: the record each of Stop's starts from */

#define NAMES_HEAD 0x0u /* the names' file (Ov002_InitResourceRecord): its header's size, then the count */
#define NAMES_FIRST_STOP 15

/* The game's languages (GetLanguage) in the data file's order (khdays_stop_languages). */
static int data_language(void)
{
    switch (GetLanguage()) {
    case 1: return 0; /* en */
    case 5: return 1; /* es */
    case 2: return 2; /* fr */
    case 3: return 3; /* de */
    case 4: return 4; /* it */
    default: return 0;
    }
}

static unsigned rd32(KhdaysRef at)
{
    return *(const volatile unsigned *)KHDAYS_W(at);
}

static unsigned rd16(KhdaysRef at)
{
    return *(const volatile unsigned short *)KHDAYS_W(at);
}

static void wr32(KhdaysRef at, unsigned value)
{
    *(volatile unsigned *)KHDAYS_W(at) = value;
}

static void wr16(KhdaysRef at, unsigned value)
{
    *(volatile unsigned short *)KHDAYS_W(at) = (unsigned short)value;
}

static KhdaysStopData data;
static int data_state; /* 0 not read yet, 1 read, -1 not usable */
static int panels_in;  /* Stop's records are in the loaded panels' table */

static const KhdaysStopData *stop_data(void)
{
    if (data_state == 0) {
        char why[200];
        data_state = -1;
        if (khdays_stop_data_read((const char *)khdays_stop_magic_text, khdays_stop_magic_text_size, &data, why,
                                  (int)sizeof(why)) != 0) {
            khdays_log("stop: the magic's data file is not read (%s): no Stop", why);
        } else if (!khdays_stop_data_panels_whole(&data)) {
            khdays_log("stop: the magic's data file lacks a level or a word: no Stop");
        } else {
            data_state = 1;
        }
    }
    return data_state == 1 ? &data : NULL;
}

/* The level (0-2) whose panel `id` is, or -1. */
static int level_of_panel(const KhdaysStopData *d, int id)
{
    for (int l = 0; d != NULL && l < KHDAYS_STOP_LEVELS; ++l) {
        if (d->levels[l].id == id) {
            return l;
        }
    }
    return -1;
}

int khdays_stop_panel(int id)
{
    return panels_in && level_of_panel(stop_data(), id) >= 0;
}

/* A UTF-8 word as the game's UTF-16 with its end (0), into `out` (room for `room`); its length with the end. */
static int game_text(const char *words, unsigned short *out, int room)
{
    const unsigned char *p = (const unsigned char *)words;
    int n = 0;
    while (*p != '\0') {
        const unsigned short c = khdays_utf8_next(&p);
        if (out != NULL && n < room) {
            out[n] = c;
        }
        ++n;
    }
    if (out != NULL && n < room) {
        out[n] = 0;
    }
    return n + 1;
}

/* Whether panel record `record` is one of the table's blank ones: a material with no number and no kind. */
static int blank_record(KhdaysRef record)
{
    return rd16(record + PANEL_CATEGORY) == CATEGORY_MATERIAL && rd16(record + PANEL_NUMBER) == 0 &&
           rd16(record + PANEL_KIND) == 0xffffu;
}

/* The strings' pack of database `db` made again with string `k` replaced by `text[k]` where it is given. */
static void replace_strings(unsigned db, const char *const *text, int first, int count_given)
{
    const KhdaysRef table = rd32(KHDAYS_DS_data_0204c238);
    const KhdaysRef old = rd32(table + db * DB_ROW + DB_STRINGS);
    if (!khdays_game_ram(old)) {
        return;
    }
    const unsigned count = rd32(old);
    if ((unsigned)(first + count_given) > count) {
        khdays_log("stop: database %#x has %u strings, not the %d Stop's words need", db, count, first + count_given);
        return;
    }
    /* each string's bytes, old or new */
    unsigned *lengths = malloc(count * sizeof(*lengths));
    unsigned total = 0;
    if (lengths == NULL) {
        return;
    }
    for (unsigned k = 0; k < count; ++k) {
        const int ours = (int)k >= first && (int)k < first + count_given ? (int)k - first : -1;
        const unsigned start = k == 0 ? 0 : rd32(old + 4u * k);
        lengths[k] = ours >= 0 ? 2u * (unsigned)game_text(text[ours], NULL, 0) : rd32(old + 4u * (k + 1)) - start;
        total += lengths[k];
    }
    const unsigned head = 4u * (count + 1);
    const KhdaysRef made = NNSi_FndAllocFromDefaultExpHeap(head + total);
    if (!khdays_game_ram(made)) {
        khdays_log("stop: no room in the game's heap for database %#x's strings (%u bytes)", db, head + total);
        free(lengths);
        return;
    }
    unsigned at = 0;
    wr32(made, count);
    for (unsigned k = 0; k < count; ++k) {
        const int ours = (int)k >= first && (int)k < first + count_given ? (int)k - first : -1;
        const KhdaysRef to = made + head + at;
        if (ours >= 0) {
            game_text(text[ours], (unsigned short *)KHDAYS_W(to), (int)(lengths[k] / 2u));
        } else {
            memcpy(KHDAYS_W(to), KHDAYS_W(old + head + (k == 0 ? 0 : rd32(old + 4u * k))), lengths[k]);
        }
        at += lengths[k];
        wr32(made + 4u * (k + 1), at);
    }
    free(lengths);
    wr32(table + db * DB_ROW + DB_STRINGS, made);
    NNSi_FndFreeFromDefaultHeap(old); /* the same heap it was just loaded from (Archive_LoadFile) */
}

/* Stop's three panels written over three blank records of the loaded panels' table, and their words. */
static void write_panels(const KhdaysStopData *d)
{
    const KhdaysRef table = rd32(KHDAYS_DS_data_0204c238);
    const KhdaysRef records = rd32(table + DB_PANELS * DB_ROW + DB_RECORDS);
    const int count = (int)rd16(table + DB_PANELS * DB_ROW + DB_COUNT);
    panels_in = 0;
    for (int l = 0; l < KHDAYS_STOP_LEVELS; ++l) {
        const int id = d->levels[l].id;
        if (id < 1 || id > count || id - 1 != d->levels[0].id - 1 + l) {
            khdays_log("stop: panel %d is not in the panels' table (%d records) or not after the first: no Stop", id,
                       count);
            return;
        }
        const KhdaysRef record = records + (unsigned)(id - 1) * PANEL_BYTES;
        if (!blank_record(record)) {
            khdays_log("stop: panel %d is not a blank record of this copy of the game: no Stop", id);
            return;
        }
    }
    for (int l = 0; l < KHDAYS_STOP_LEVELS; ++l) {
        const KhdaysRef record = records + (unsigned)(d->levels[l].id - 1) * PANEL_BYTES;
        const KhdaysRef blizzard = records + (unsigned)(BLIZZARD - 1 + l) * PANEL_BYTES;
        const unsigned text_id = rd16(record + 0x60u); /* the blank record's own words' number stays */
        memcpy(KHDAYS_W(record), KHDAYS_W(blizzard), PANEL_BYTES);
        wr16(record + PANEL_KIND, 0xffffu); /* no parameter: Stop's casts are the port's (the battle part) */
        *(volatile unsigned char *)KHDAYS_W(record + PANEL_MAGIC) = 0; /* nor a magic of the game's 15 */
        wr32(record + PANEL_PRICE, (unsigned)d->levels[l].price);
        wr16(record + 0x60u, text_id);
    }
    /* the words: each panel's name and description, strings 2 (id - 1) and 2 (id - 1) + 1 */
    const int g = data_language();
    const char *text[2 * KHDAYS_STOP_LEVELS];
    for (int l = 0; l < KHDAYS_STOP_LEVELS; ++l) {
        text[2 * l] = d->words[g][l].name;
        text[2 * l + 1] = d->words[g][l].panel;
    }
    replace_strings(DB_PANELS, text, 2 * (d->levels[0].id - 1), 2 * KHDAYS_STOP_LEVELS);
    panels_in = 1;
    khdays_log("stop: panels %d-%d written (%s)", d->levels[0].id, d->levels[KHDAYS_STOP_LEVELS - 1].id,
               khdays_stop_languages[g]);
}

void khdays_stop_db_loaded(int id)
{
    const KhdaysStopData *d = stop_data();
    if (d != NULL && id == DB_PANELS) {
        write_panels(d);
    }
}

/* The level (0-2) whose name is name `i` of the battle's list when the game left it empty (its size and a 0), or -1. */
static int empty_stop_name(unsigned i, unsigned size)
{
    const int l = (int)i - NAMES_FIRST_STOP;
    return l >= 0 && l < KHDAYS_STOP_LEVELS && size <= 6u ? l : -1;
}

void khdays_stop_magic_names(void *record)
{
    const KhdaysStopData *d = stop_data();
    const KhdaysRef r = KHDAYS_REF(record);
    const KhdaysRef old = rd32(r);
    if (d == NULL || !khdays_game_ram(old)) {
        return;
    }
    const unsigned head = rd32(old + NAMES_HEAD), count = rd32(old + 4u);
    const int g = data_language();
    /* the file's size with the port's names where the game's are empty: each entry its size, then its text */
    unsigned total = head, filled = 0;
    KhdaysRef at = old + head;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned size = rd32(at);
        const int l = empty_stop_name(i, size);
        if (l >= 0) {
            total += 4u + 2u * (unsigned)game_text(d->words[g][l].name, NULL, 0);
            ++filled;
        } else {
            total += size;
        }
        at += size;
    }
    if (filled == 0) {
        return; /* the game names Stop in this language */
    }
    const KhdaysRef made = NNSi_FndAllocFromDefaultExpHeap(total);
    if (!khdays_game_ram(made)) {
        return;
    }
    memcpy(KHDAYS_W(made), KHDAYS_W(old), head);
    KhdaysRef from = old + head, to = made + head;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned size = rd32(from);
        const int l = empty_stop_name(i, size);
        if (l >= 0) {
            const char *name = d->words[g][l].name;
            const unsigned now = 4u + 2u * (unsigned)game_text(name, NULL, 0);
            wr32(to, now);
            game_text(name, (unsigned short *)KHDAYS_W(to + 4u), (int)(now - 4u) / 2);
            to += now;
        } else {
            memcpy(KHDAYS_W(to), KHDAYS_W(from), size);
            to += size;
        }
        from += size;
    }
    wr32(r, made);
    wr32(r + 8u, made + head); /* the record: the file, its count, its first name */
    NNSi_FndFreeFromDefaultHeap(old);
    khdays_log("stop: %u of the battle's magic names filled (%s)", filled, khdays_stop_languages[g]);
}

/* --- The battle --------------------------------------------------------------------------------------------------
 * An actor's pool of magic slots has 15 places and kept the cast's level animation (its grid entry) in the word after
 * them: the port keeps that word (khdays_magic_track), so the place after the 15 is Stop's, 15. Stop's slot is built
 * with Cure's constructor over Stop's own model and takes Cure's handlers (its effect stands round its caster, as
 * Cure's does), with a kind of its own (15) so Cure's heal does not run. Its three levels are the magic submenu's
 * cells 15-17, whose names the battle's list has; its casts are the port's own (the party record keeps casts only for
 * parameters 1-53, and 16 is Dodge Roll's). Cast, it marks a zone round its caster; each enemy standing in it (not
 * one the area keeps idle), on its next tick, is held as a frozen enemy is (Ov107_ProcessObjectTick) for the level's
 * time less its own resistance to freezing, unless freezing cannot touch it; the damage its hits would have dealt is
 * kept and dealt at once as it moves again, as a burn's timed hit is (flags 0x88/0x80). docs/RUNTIME_MAP.md,
 * "The magic". */

extern int Session_GetLocalPlayerIndex(void);
extern void Ov002_HudSetSlotValue(int key, int value);

#define STOP_SLOT 15            /* the pool's place after the 15 magics' */
#define STOP_CELL 15            /* the submenu's cells (and names) 15-17 */
#define CURE_TEMPLATES (12 * 5) /* ba/ch/mp.z: Cure's templates, count 1 first */
#define DECK_CELLS (3 * 8 * 5)  /* deck_link.h */
#define POOL_CAST 0xcu          /* the pool's slot being cast (Ov022_SetRecordValue) */
#define POOL_SLOTS 0x18u        /* the pool's slots (Ov022_InitEntryState) */
#define POOL_ACTOR 0x58u
#define SLOT_ANIMATION 0x8u   /* a slot's animation state (BindAnimTrack): */
#define ANIMATION_BOUND 0xcu  /* the 5 animation objects bound, */
#define ANIMATION_MODEL 0x20u /* its model's NNSG3dRenderObj: anmMat 8, anmJnt 0x10, anmVis 0x18, */
#define MODEL_HINTS 0x3cu     /* and the three hints of which each animates, 0x3c-0x54 */
#define ACTOR_ID 9u
#define ACTOR_PLACE 0x48cu         /* an actor's place, VecFx32 (Ov022_EnterBlockReaction) */
#define ENEMY_PLACE 0x74u          /* an enemy's place, VecFx32 (Ov107_ProcessObjectTick) */
#define ENEMY_FLAGS 0x60u          /* 0x100 standing: set going, not idle in the area's table */
#define ENEMY_HP 0x21au            /* s16, its most at 0x218 (Ov107_AiState_ApplyHit) */
#define ENEMY_STATUS 0x1c4u        /* 1 burning, 2 frozen, 4 jolted, 8 air-tossed, 0x10 not to be hit */
#define ENEMY_IMMUNE 0x1b0u        /* bit 3: freezing cannot touch it (Ov107_AiState_ApplyHit's masks 7, 3, 11, 2) */
#define ENEMY_FREEZE_RESIST 0x1b8u /* +0x1b4's second, freezing's: 0x1000 whole */

typedef struct Casts {
    int most[KHDAYS_STOP_LEVELS], left[KHDAYS_STOP_LEVELS];
    int level; /* the level its last cast chose */
} Casts;

static Casts *casts; /* by player */
static int casts_count;
static int measuring;
static int release_hp; /* the enemy's health as its kept damage is dealt (khdays_stop_release, _released) */

static Casts *casts_of(int player)
{
    if (player < 0) {
        return NULL;
    }
    if (player >= casts_count) {
        Casts *grown = realloc(casts, (size_t)(player + 1) * sizeof(*casts));
        if (grown == NULL) {
            return NULL;
        }
        memset(grown + casts_count, 0, (size_t)(player + 1 - casts_count) * sizeof(*casts));
        casts = grown;
        casts_count = player + 1;
    }
    return &casts[player];
}

/* Each pool's level animation entry (the word after its 15 slots in the game's pool). */
typedef struct Track {
    KhdaysRef pool;
    int entry;
} Track;

static Track *tracks;
static int tracks_count, tracks_room;

static Track *track_of(KhdaysRef pool)
{
    for (int i = 0; i < tracks_count; ++i) {
        if (tracks[i].pool == pool) {
            return &tracks[i];
        }
    }
    return NULL;
}

int khdays_magic_track(KhdaysRef pool)
{
    const Track *t = track_of(pool);
    return t != NULL ? t->entry : 0;
}

void khdays_magic_track_set(KhdaysRef pool, int entry)
{
    Track *t = track_of(pool);
    if (entry == 0) {
        if (t != NULL) {
            *t = tracks[--tracks_count];
        }
        return;
    }
    if (t == NULL) {
        if (tracks_count == tracks_room) {
            const int room = tracks_room > 0 ? 2 * tracks_room : 8;
            Track *grown = realloc(tracks, (size_t)room * sizeof(*tracks));
            if (grown == NULL) {
                khdays_hal_unimplemented("no memory for a magic's level animation");
            }
            tracks = grown;
            tracks_room = room;
        }
        t = &tracks[tracks_count++];
        t->pool = pool;
    }
    t->entry = entry;
}

static int pool_actor(KhdaysRef pool)
{
    return *(const volatile unsigned char *)KHDAYS_W(rd32(pool + POOL_ACTOR) + ACTOR_ID);
}

/* The local player's casts, folded from the deck as the battle builds their magic: a cast for each Stop panel set,
 * as each magic panel gives its magic one. */
static void fold_casts(int player)
{
    const KhdaysStopData *d = stop_data();
    Casts *c = casts_of(player);
    unsigned short cells[DECK_CELLS];
    if (d == NULL || c == NULL || !panels_in) {
        return;
    }
    memset(c, 0, sizeof(*c));
    if (!khdays_link_deck(cells)) {
        return;
    }
    for (int i = 0; i < DECK_CELLS; ++i) {
        const int l = level_of_panel(d, cells[i]);
        if (l >= 0) {
            ++c->most[l];
        }
    }
    for (int l = 0; l < KHDAYS_STOP_LEVELS; ++l) {
        c->left[l] = c->most[l];
    }
    khdays_log("stop: player %d casts %d %d %d", player, c->most[0], c->most[1], c->most[2]);
}

int khdays_stop_slot_wanted(int actor, char *path, int *parts, int *template_index)
{
    const Casts *c;
    if (actor != Session_GetLocalPlayerIndex() || stop_data() == NULL || !panels_in) {
        return 0;
    }
    fold_casts(actor);
    c = casts_of(actor);
    if (c == NULL || c->most[0] + c->most[1] + c->most[2] == 0) {
        return 0;
    }
    strcpy(path, "ba/ma/st.p.z");
    *parts = 1;
    *template_index = CURE_TEMPLATES;
    khdays_log("stop: actor %d's slot built (Cure's constructor, Stop's model)", actor);
    return 1;
}

int khdays_stop_cells(void *session, int count)
{
    const Casts *c = casts_of(Session_GetLocalPlayerIndex());
    unsigned char *cells = (unsigned char *)session + 0x32;
    for (int l = 0; c != NULL && l < KHDAYS_STOP_LEVELS && count < 15; ++l) {
        if (c->most[l] > 0) {
            cells[2 * count] = (unsigned char)(STOP_CELL + l);
            ++count;
        }
    }
    return count;
}

int khdays_stop_cell_mask(void)
{
    const Casts *c = casts_of(Session_GetLocalPlayerIndex());
    int mask = 0;
    for (int l = 0; c != NULL && l < KHDAYS_STOP_LEVELS; ++l) {
        if (c->left[l] > 0) {
            mask |= 1 << (STOP_CELL + l);
        }
    }
    return mask;
}

void khdays_stop_hud(int actor, int *mask)
{
    const Casts *c = casts_of(actor);
    for (int l = 0; c != NULL && l < KHDAYS_STOP_LEVELS; ++l) {
        if (c->most[l] > 0) {
            Ov002_HudSetSlotValue(STOP_CELL + l, c->left[l]);
            if (c->left[l] > 0) {
                *mask |= 1 << (STOP_CELL + l);
            }
        }
    }
}

int khdays_stop_casts_left(KhdaysRef pool, int level)
{
    const Casts *c = casts_of(pool_actor(pool));
    return c != NULL && level >= 0 && level < KHDAYS_STOP_LEVELS ? c->left[level] : 0;
}

int khdays_stop_record(KhdaysRef pool, int value)
{
    Casts *c;
    if (value < STOP_CELL || value >= STOP_CELL + KHDAYS_STOP_LEVELS) {
        return value;
    }
    c = casts_of(pool_actor(pool));
    if (c != NULL) {
        c->level = value - STOP_CELL;
    }
    return STOP_SLOT;
}

/* Stop's slot forgets the animations bound to its model, whose objects lie in the level animation's buffer about to
 * be loaded again. The game's own slots each bind one level's animation, loaded again to the same places, so the game
 * removes the old ones by walking its model's list (BindAnimTrack); Stop's binds three in turn, and the old list would
 * be another file's bytes. */
static void forget_animations(KhdaysRef pool)
{
    const KhdaysRef slot = rd32(pool + POOL_SLOTS + 4u * STOP_SLOT);
    if (slot == 0) {
        return;
    }
    const KhdaysRef state = slot + SLOT_ANIMATION, model = state + ANIMATION_MODEL;
    for (unsigned i = 0; i < 5; ++i) {
        wr32(state + ANIMATION_BOUND + 4u * i, 0);
    }
    wr32(model + 0x08u, 0);
    wr32(model + 0x10u, 0);
    wr32(model + 0x18u, 0);
    for (unsigned at = MODEL_HINTS; at < MODEL_HINTS + 0x18u; at += 4u) {
        wr32(model + at, 0);
    }
}

void khdays_stop_animation(KhdaysRef pool, char *path)
{
    char *at = path != NULL ? strstr(path, "ba/ma/s0_mo") : NULL;
    const Casts *c = at != NULL ? casts_of(pool_actor(pool)) : NULL;
    if (c != NULL) {
        at[7] = (char)('0' + c->level);
        forget_animations(pool);
    }
}

int khdays_stop_cast(KhdaysRef pool)
{
    const KhdaysStopData *d = stop_data();
    if (d == NULL || (int)rd32(pool + POOL_CAST) != STOP_SLOT) {
        return 0;
    }
    const int actor = pool_actor(pool);
    Casts *c = casts_of(actor);
    if (c == NULL) {
        return 0;
    }
    const int l = c->level;
    const KhdaysRef caster = rd32(pool + POOL_ACTOR);
    if (c->left[l] > 0) {
        --c->left[l];
    }
    const int x = (int)rd32(caster + ACTOR_PLACE), z = (int)rd32(caster + ACTOR_PLACE + 8u);
    const int reach = (int)(d->levels[l].reach * 4096.0);
    khdays_stop_hold_cast(x, z, reach, d->levels[l].ticks, khdays_link_frame());
    khdays_log("stop: %s cast by actor %d at %d %d, reach %d, %d left", d->words[0][l].name, actor, x, z, reach,
               c->left[l]);
    return 1;
}

int khdays_stop_holding(void *enemy)
{
    const KhdaysRef e = KHDAYS_REF(enemy);
    const unsigned status = *(const volatile unsigned char *)KHDAYS_W(e + ENEMY_STATUS);
    KhdaysStopEnemy seen;
    int taken, freed, held;
    seen.x = (int)rd32(e + ENEMY_PLACE);
    seen.z = (int)rd32(e + ENEMY_PLACE + 8u);
    seen.standing = (rd16(e + ENEMY_FLAGS) & 0x100u) != 0;
    seen.untouchable = (status & 0x10u) != 0 || (rd32(e + ENEMY_IMMUNE) & (1u << 3)) != 0;
    seen.resist = (int)rd32(e + ENEMY_FREEZE_RESIST);
    held = khdays_stop_hold_tick(e, &seen, khdays_link_frame(), &taken, &freed);
    if (taken > 0) {
        khdays_log("stop: enemy %#x held for %d ticks (resistance %#x)", e, taken, seen.resist);
    }
    if (freed) {
        khdays_log("stop: enemy %#x released, %d damage kept", e, khdays_stop_hold_kept(e));
    }
    return held;
}

int khdays_stop_held(void *enemy)
{
    return khdays_stop_hold_held(KHDAYS_REF(enemy));
}

int khdays_stop_release(void *enemy, int *amount)
{
    const KhdaysRef e = KHDAYS_REF(enemy);
    if (!khdays_stop_hold_due(e, amount)) {
        return 0;
    }
    release_hp = (short)rd16(e + ENEMY_HP);
    return 1;
}

/* The kept damage's hit made: dealt when the enemy's health went down (a hit's grace may drop it: again next tick). */
void khdays_stop_released(void *enemy)
{
    const KhdaysRef e = KHDAYS_REF(enemy);
    if ((short)rd16(e + ENEMY_HP) < release_hp) {
        khdays_log("stop: enemy %#x took its kept %d", e, khdays_stop_hold_kept(e));
        khdays_stop_hold_dealt(e);
    }
}

void khdays_stop_enemy_gone(void *enemy)
{
    khdays_stop_hold_forget(KHDAYS_REF(enemy));
}

void khdays_stop_area_left(void)
{
    khdays_stop_hold_clear();
}

void khdays_stop_measure(int on)
{
    measuring = on;
}

int khdays_stop_store(void *enemy, int amount)
{
    return khdays_stop_hold_store(KHDAYS_REF(enemy), amount);
}

int khdays_stop_damage(void *enemy, int amount)
{
    return measuring || !khdays_stop_held(enemy) ? amount : 0;
}
