/* A guest's prediction of its own character, off by default (KHDAYS_NET_PREDICT=1 turns it on: online
 * the host corrected it often, once by 46 units after an area change, and the character fell through
 * the floor). The battle runs on the host, so a guest's own character reacts a round trip late; with
 * the prediction the guest runs it itself, as a host does, through prepare.py's hooks:
 *   - ROLE_QUERY: the game's "am I the host?" checks answer host while the own character updates
 *     (not ROLE_KEPT's: those touching the others or the world);
 *   - Ov022_Party_SyncAndUpdate: the own character's update is the window (khdays_mp_predict_open);
 *   - Ov022_Party_ApplyNetState: the host's record is compared with the predicted path
 *     (khdays_mp_predict_keep): moved by the difference, or the host's version shown during a hit;
 *   - MsgQueue_SendGate: what the game sends in the window is left to the host, but for its shots,
 *     applied at once (the host's copies are filtered out when they come).
 * docs/RUNTIME_MAP.md, "A guest's own character, predicted", has the details and measurements. */
#include "net_session.h"
#include "../core/session_log.h"
#include "../core/window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* From the decompilation: the local player's index (0 the host), the base of the battle's input table
 * on a host (on a guest the same memory holds the host's states), and the copy of a table row into an
 * actor (Ov022_RefreshSlotRows calls it on a host). */
extern int Session_GetLocalPlayerIndex(void);
extern int Ov022_GetGlobalPlus14(void);
extern void Ov022_LoadNodeFromResource(int node, int index);
extern int Ov022_ActorSetState(int actor, int state);
extern int GetEntryField20ByIndex(int index);

/* A control packet (Ov022_SendControlPacket) is 12 bytes; its second
 * halfword holds the slot (bits 0-2) and the buttons held (bits 3-14).
 * The input table, from its base (Ov022_PublishRequestToSlot,
 * func_ov022_0208a1fc): a "taken" flag at +0x10 + slot, the packet at
 * +0x14 + slot * 12, and the buttons held, pressed and held before at
 * +0x84, +0x86 and +0x88 + slot * 6. */
enum { CONTROL = 12, SLOTS = 8 };

static int enabled = -1;                /* -1 until KHDAYS_NET_PREDICT is read */
static int open_now, paused;            /* the window (the own character's update) is open / paused */
static unsigned short held_before[SLOTS];
/* counts for the session log (every 150 ticks) */
static unsigned int ticks, kept, followed, reactions, report_at = 150u;
static unsigned int moved;              /* host records off the path (the character was moved) */
static unsigned int unmatched;          /* host records with an animation the path never showed */
static unsigned int turned;             /* host records with another heading (the character was turned) */
static unsigned int dropped[32];        /* messages left to the host, by gate */
static int shift[3];                    /* the move khdays_mp_predict_shift applies */
/* following: the host decided a reaction (a hit), so the host's version of
 * the character is shown, as the game shows a guest's. restarting: the
 * reaction is over, the prediction restarts from the idle state. */
static int following, restarting;
static unsigned int diverged;           /* times the host ran something the prediction did not */
/* The idle state (func_ov022_020a37d0: state 0 of Ov022_EnterActorState, the
 * one Ov022_ActorSetState falls back to), as the actor's state word holds it,
 * and the animations (+0x6bc) the own character shows while idle (standing,
 * running). When the host's version shows one of these, the prediction can
 * take over again. They are learnt while predicting. */
enum { IDLE_STATE = 0x020a37d0 };
static int *free_animations;
static unsigned int free_count, free_room;
/* how many host records in a row had an animation the path never showed,
 * and how far ahead of the host (in ticks) the path was at the last match */
static unsigned int unmatched_run, lead_at_match;
/* The predicted path: the own character's positions since the host's record
 * last matched one. Each step is STEP ints: x, y, z (fx32), the animation
 * (+0x6bc), the tick, and the heading (the node's angle at +0x80 minus
 * 0x8000, as func_ov022_02088254 puts it in the host's record). The host runs
 * the same buttons a round trip later, so its record is compared with the
 * closest step that shows the same animation. */
enum { STEP = 6 };
static int *path;
static unsigned int path_count, path_room;
static int farthest;                    /* the farthest a host record was off the path, fx32 */
static int current;                     /* the own character's actor, while the window is open */

/* The own character's shots the guest applied at once (gate 9, the commands
 * of Ov022_RunShotCommand: operation, owner and mode in the first byte, the
 * channel in the second, the shot or its descriptor in the third and fourth),
 * each with the tick it was made in. Each is kept until the host's copy comes
 * (and is removed), or until the host's records are past that tick without
 * it (the host never made that shot). */
enum { SHOT_GATE = 9, SHOT_ROOM = 32, SHOT_KEY = 4 };
typedef struct OwnShot {
    unsigned char data[SHOT_ROOM];
    unsigned int size, tick;
} OwnShot;
static OwnShot *shots;
static unsigned int shot_count, shot_room;
static unsigned int shots_made, shots_matched, shots_differed, shots_expired, shots_refunded;
static unsigned int host_tick;          /* the path tick the host's record matched last */
/* Host copies of the own character's shots that arrived before the prediction made them (the host can
 * run a press a tick before the guest's own prediction does): applied as they come, the prediction's
 * own copy then skipped. */
static OwnShot *ahead;
static unsigned int ahead_count, ahead_room, shots_late;
static int own_owner = -1;              /* the own character's party index (+0x9): the owner of its shots */
static unsigned int leave_out_own(unsigned char *d, unsigned int size);

/* Whether `animation` is one the own character shows while idle (until any is
 * learnt, only animation 0). */
static int free_animation(int animation)
{
    if (free_count == 0) {
        return animation == 0;
    }
    for (unsigned int n = 0; n < free_count; ++n) {
        if (free_animations[n] == animation) {
            return 1;
        }
    }
    return 0;
}

/* While the actor is in its idle state, remembers its animation as idle. */
static void learn_free(int actor)
{
    unsigned int state;
    int animation;
    memcpy(&state, (const unsigned char *)KHDAYS_W(actor) + 0x460, 4);
    memcpy(&animation, (const unsigned char *)KHDAYS_W(actor) + 0x6bc, 4);
    if (state != IDLE_STATE || animation < 0 || (free_count != 0 && free_animation(animation))) {
        return;
    }
    if (free_count == free_room) {
        const unsigned int room = free_room != 0 ? free_room * 2u : 8u;
        int *grown = (int *)realloc(free_animations, (size_t)room * sizeof(int));
        if (grown == NULL) {
            return;
        }
        free_animations = grown;
        free_room = room;
    }
    free_animations[free_count++] = animation;
}

/* KHDAYS_TRACE_PREDICT=1 logs the own character's state at the start of each
 * update, whenever it changes. */
static int trace = -1;

static int tracing(void)
{
    if (trace < 0) {
        const char *value = getenv("KHDAYS_TRACE_PREDICT");
        trace = value != NULL && value[0] == '1';
    }
    return trace;
}

static int on(void)
{
    if (enabled < 0) {
        /* on only with KHDAYS_NET_PREDICT=1 (see the top of the file) */
        const char *value = getenv("KHDAYS_NET_PREDICT");
        enabled = value != NULL && value[0] == '1';
    }
    return enabled;
}

/* The local player's index as the game should see it: 0 (the host) while the
 * own character's update runs, the real index otherwise. */
int khdays_mp_role(void)
{
    return open_now && !paused ? 0 : Session_GetLocalPlayerIndex();
}

/* Every 150 battle ticks (about 5 s), writes the counts to the session log. */
static void report(void)
{
    if (ticks < report_at) {
        return;
    }
    {
        char gates[160];
        int used = 0;
        gates[0] = '\0';
        for (unsigned int g = 0; g < 32u; ++g) {
            if (dropped[g] != 0 && used < (int)sizeof(gates) - 16) {
                used += snprintf(gates + used, sizeof(gates) - (size_t)used, " %u:%u", g, dropped[g]);
            }
        }
        khdays_log("net: prediction: 150 ticks of its own character predicted, the host's state kept off %u times and "
                   "followed %u (%u reactions of the host's, %u where it ran otherwise); the host's position off the "
                   "predicted path %u times "
                   "(%.2f units at most, moved by it), its heading %u times (turned to it), %u records with no "
                   "predicted animation like theirs; its "
                   "shots applied at once %u, the host's copies left out %u (%u not the same), %u the host never "
                   "made (%u of their spends given back), %u whose host's copy came first; its other messages left "
                   "to the host by gate (gate:count)%s",
                   kept, followed, reactions, diverged, moved, farthest / 4096.0, turned, unmatched, shots_made,
                   shots_matched,
                   shots_differed, shots_expired, shots_refunded, shots_late, used != 0 ? gates : " none");
    }
    kept = followed = reactions = diverged = moved = turned = unmatched = 0;
    shots_made = shots_matched = shots_differed = shots_expired = shots_refunded = shots_late = 0;
    memset(dropped, 0, sizeof(dropped));
    farthest = 0;
    report_at = ticks + 150u; /* about 5 s of battle ticks */
}

/* Trace only: logs the own character's state when its update starts, if it
 * changed: buttons and pending action (+0x18..+0x1c), command (+0x4d6), state
 * function (+0x460), flag words (+0, +0x464), position and step (+0x48c,
 * +0x498). */
static void trace_actor(int actor)
{
    static unsigned char last[64];
    unsigned char now[64];
    const unsigned char *a = (const unsigned char *)KHDAYS_W(actor);
    memset(now, 0, sizeof(now));
    memcpy(now, a + 0x18, 6);
    memcpy(now + 6, a + 0x4d6, 1);
    memcpy(now + 8, a + 0x460, 4);
    memcpy(now + 12, a, 8);
    memcpy(now + 20, a + 0x464, 8);
    memcpy(now + 28, a + 0x48c, 12);
    memcpy(now + 40, a + 0x498, 12);
    {
        /* the first shot channel's budget (+0x18 of the channel at +0x2644, Ov022_RunShotCommand) */
        unsigned int channel;
        memcpy(&channel, a + 0x2644, 4);
        now[52] = channel != 0 ? *(const unsigned char *)KHDAYS_W(channel + 0x18) : 0xff;
    }
    if (memcmp(now, last, sizeof(now)) != 0) {
        unsigned int state;
        unsigned long long flags, flags2;
        int pos[3], step[3];
        memcpy(&state, now + 8, 4);
        memcpy(&flags, now + 12, 8);
        memcpy(&flags2, now + 20, 8);
        memcpy(pos, now + 28, 12);
        memcpy(step, now + 40, 12);
        khdays_log("predict: actor pressed %04x held %04x pending %u command %d state %08x flags %016llx flags2 "
                   "%016llx at %d,%d,%d step %d,%d,%d budget %u",
                   now[0] | now[1] << 8, now[2] | now[3] << 8, now[4] | now[5] << 8, (signed char)now[6], state,
                   flags, flags2, pos[0], pos[1], pos[2], step[0], step[1], step[2], now[52]);
        memcpy(last, now, sizeof(last));
    }
}

/* Trace only: logs the host's record of the own character when it changed: event word (+0, the host's
 * +0x464), animation index (+0x1c), HP (+0x20), animation step (+0x22), the top 4 bits of +0x26 (the
 * actor's +0x2770: 14 is standing) and the packed word at +0x28 (bit 24 the action lock). */
static void trace_record(const unsigned char *r)
{
    static unsigned char last[16];
    unsigned char now[16];
    memset(now, 0, sizeof(now));
    memcpy(now, r, 8);
    memcpy(now + 8, r + 0x1c, 2);
    memcpy(now + 10, r + 0x20, 4);
    memcpy(now + 14, r + 0x26, 2);
    if (memcmp(now, last, sizeof(now)) != 0) {
        unsigned long long state;
        unsigned int packed;
        short reaction, step;
        unsigned short hp, word26;
        memcpy(&state, r, 8);
        memcpy(&reaction, r + 0x1c, 2);
        memcpy(&hp, r + 0x20, 2);
        memcpy(&step, r + 0x22, 2);
        memcpy(&word26, r + 0x26, 2);
        memcpy(&packed, r + 0x28, 4);
        khdays_log("predict: host's record state %016llx animation %d hp %u step %d stance %u packed %08x",
                   state, reaction, hp, step, word26 >> 12, packed);
        memcpy(last, now, sizeof(last));
    }
}

/* Hands the own character its last control the way a host's input table
 * does, then puts the table memory back as it was (on a guest that memory
 * holds the host's states). */
static void hand_control(int node)
{
    unsigned char packet[32], saved_taken, saved_row[CONTROL], saved_buttons[6];
    unsigned char *base;
    unsigned int slot, held;
    if (khdays_net_own_control(packet, sizeof(packet)) != CONTROL) {
        return;
    }
    slot = (unsigned int)(packet[2] | packet[3] << 8) & 7u;
    held = ((unsigned int)(packet[2] | packet[3] << 8) >> 3) & 0xfffu;
    base = (unsigned char *)KHDAYS_W(Ov022_GetGlobalPlus14());
    saved_taken = base[0x10 + slot];
    memcpy(saved_row, base + 0x14 + slot * CONTROL, CONTROL);
    memcpy(saved_buttons, base + 0x84 + slot * 6, 6);
    base[0x10 + slot] = 1;
    memcpy(base + 0x14 + slot * CONTROL, packet, CONTROL);
    {
        const unsigned short now = (unsigned short)held, pressed = (unsigned short)(held & ~held_before[slot]);
        memcpy(base + 0x84 + slot * 6, &now, 2);
        memcpy(base + 0x86 + slot * 6, &pressed, 2);
        memcpy(base + 0x88 + slot * 6, &held_before[slot], 2);
        held_before[slot] = now;
    }
    Ov022_LoadNodeFromResource(node, (int)slot);
    base[0x10 + slot] = saved_taken;
    memcpy(base + 0x14 + slot * CONTROL, saved_row, CONTROL);
    memcpy(base + 0x84 + slot * 6, saved_buttons, 6);
}

/* While the host's version is shown, keeps track of the own buttons, so that
 * the first predicted tick afterwards can tell a new press from a hold. */
static void follow_control(void)
{
    unsigned char packet[32];
    if (khdays_net_own_control(packet, sizeof(packet)) == CONTROL) {
        const unsigned int word = (unsigned int)(packet[2] | packet[3] << 8);
        held_before[word & 7u] = (unsigned short)((word >> 3) & 0xfffu);
    }
}

/* The character's heading as the host's record holds it (+0x18,
 * func_ov022_02088254): the angle at +0x80 of its node (+0x20), minus 0x8000. */
static int heading_of(int actor)
{
    unsigned int node;
    unsigned short angle;
    memcpy(&node, (const unsigned char *)KHDAYS_W(actor) + 0x20, 4);
    if (node == 0) {
        return 0;
    }
    memcpy(&angle, (const unsigned char *)KHDAYS_W(node) + 0x80, 2);
    return (unsigned short)(angle - 0x8000u);
}

/* Turns the character by `difference` (in the angle's units). */
static void turn_by(int actor, int difference)
{
    unsigned int node;
    unsigned short angle;
    memcpy(&node, (const unsigned char *)KHDAYS_W(actor) + 0x20, 4);
    if (node == 0) {
        return;
    }
    memcpy(&angle, (const unsigned char *)KHDAYS_W(node) + 0x80, 2);
    angle = (unsigned short)(angle + difference);
    memcpy((unsigned char *)KHDAYS_W(node) + 0x80, &angle, 2);
}

/* Adds the character's current position, animation, tick and heading to the
 * predicted path. */
static void remember(int actor)
{
    if (path_count == path_room) {
        const unsigned int room = path_room != 0 ? path_room * 2u : 64u;
        int *grown = (int *)realloc(path, (size_t)room * STEP * sizeof(int));
        if (grown == NULL) {
            path_count = 0; /* out of memory: start the path again rather than stop the game */
            return;
        }
        path = grown;
        path_room = room;
    }
    memcpy(path + path_count * STEP, (const unsigned char *)KHDAYS_W(actor) + 0x48c, 3u * sizeof(int));
    memcpy(path + path_count * STEP + 3, (const unsigned char *)KHDAYS_W(actor) + 0x6bc, sizeof(int));
    path[path_count * STEP + 4] = (int)ticks;
    path[path_count * STEP + 5] = heading_of(actor);
    ++path_count;
}

/* The game is about to update character `node` (`local` is our player
 * index). For the own character this opens the window: it gets its last
 * control, and runs as a host's character until khdays_mp_predict_close. */
void khdays_mp_predict_open(int node, int local)
{
    static int filtering;
    if (!on()) {
        return;
    }
    if (!filtering) {
        filtering = 1;
        khdays_net_filter(leave_out_own);
    }
    if (local == 0) {
        /* on the host: a guest's character (only traced) */
        if (tracing() && *(const unsigned char *)KHDAYS_W(node + 8) != 0) {
            trace_actor(node);
        }
        return;
    }
    if (*(const unsigned char *)KHDAYS_W(node + 8) != (unsigned char)local) {
        return;
    }
    if (following) {
        /* the host's version is shown (the game's own way) */
        follow_control();
        return;
    }
    hand_control(node);
    own_owner = *(const unsigned char *)KHDAYS_W(node + 9) & 3; /* the owner in Ov022_SendPlacementMessage */
    if (tracing()) {
        trace_actor(node);
    }
    open_now = 1;
    paused = 0;
    ++ticks;
    if (restarting) {
        /* restart from the idle state, as the game does with a character it
         * places (Ov022_ResetActorAfterPlace) */
        restarting = 0;
        *(int *)KHDAYS_W(node + 0x460) = Ov022_ActorSetState(node, 0);
    }
    current = node;
}

int khdays_mp_predict_is_open(void)
{
    return open_now;
}

/* The own character's update is over: record where it went. */
void khdays_mp_predict_close(void)
{
    if (!open_now) {
        return;
    }
    open_now = 0;
    remember(current);
    learn_free(current);
    report();
}

/* Pauses the window (while the command menu runs) or resumes it. */
void khdays_mp_predict_pause(int pause)
{
    paused = pause;
}

/* Whether two shot commands match: same_shot byte for byte (the host running
 * the same press makes the same command), like_shot by their first four bytes
 * only (operation, owner, mode, channel, shot or kind), for when the host ran
 * it a little differently. */
static int same_shot(const OwnShot *a, const unsigned char *b, unsigned int size)
{
    return a->size == size && memcmp(a->data, b, size) == 0;
}

static int like_shot(const OwnShot *a, const unsigned char *b, unsigned int size)
{
    return a->size == size && memcmp(a->data, b, SHOT_KEY) == 0;
}

/* Adds a shot command to `list`, with the current tick. Returns 0 if it
 * cannot be kept. */
static int keep_shot(OwnShot **list, unsigned int *count, unsigned int *room, const void *data, unsigned int size)
{
    if (size < SHOT_KEY || size > SHOT_ROOM) {
        return 0;
    }
    if (*count == *room) {
        const unsigned int grown_room = *room != 0 ? *room * 2u : 16u;
        OwnShot *grown = (OwnShot *)realloc(*list, (size_t)grown_room * sizeof(OwnShot));
        if (grown == NULL) {
            return 0; /* out of memory: leave the shot to the host, as without prediction */
        }
        *list = grown;
        *room = grown_room;
    }
    memcpy((*list)[*count].data, data, size);
    (*list)[*count].size = size;
    (*list)[*count].tick = ticks;
    ++*count;
    return 1;
}

/* Removes entry `n` from a list of shots. */
static void take_shot(OwnShot *list, unsigned int *count, unsigned int n)
{
    memmove(&list[n], &list[n + 1], (size_t)(*count - n - 1) * sizeof(OwnShot));
    --*count;
}

/* Forgets the early host copies the prediction is too far past to make. */
static void forget_ahead(void)
{
    unsigned int kept_ahead = 0;
    for (unsigned int n = 0; n < ahead_count; ++n) {
        /* the prediction makes its own copy within the ticks it leads by, if ever */
        if (ahead[n].tick + lead_at_match + 4u >= ticks) {
            ahead[kept_ahead++] = ahead[n];
        }
    }
    ahead_count = kept_ahead;
}

/* The game wants to send a message while the window is open. Returns 0 to
 * send it, 1 to drop it (the host sends it), 2 to apply it here at once and
 * not send it (an own shot). */
int khdays_mp_predict_drops(int gate, const void *data, int size)
{
    if (!open_now || paused) {
        return 0;
    }
    if (gate == SHOT_GATE && size >= SHOT_KEY && size <= SHOT_ROOM) {
        forget_ahead();
        for (unsigned int n = 0; n < ahead_count; ++n) {
            if (same_shot(&ahead[n], (const unsigned char *)data, (unsigned int)size)) {
                /* the host's copy came first and was applied: skip this one */
                take_shot(ahead, &ahead_count, n);
                ++shots_late;
                return 1;
            }
        }
        if (keep_shot(&shots, &shot_count, &shot_room, data, (unsigned int)size)) {
            ++shots_made;
            if (tracing()) {
                const unsigned char *b = (const unsigned char *)data;
                khdays_log("predict: own shot at tick %u: %02x %02x %02x %02x %02x %02x %02x %02x (%d bytes, %u held)",
                           ticks, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], size, shot_count);
            }
            return 2; /* applied here at once (prepare.py's join); the host's copy will be removed */
        }
    }
    ++dropped[(unsigned int)gate & 31u];
    return 1;
}

/* The filter for each record from the host (khdays_net_filter): removes the
 * host's copies of shots the guest already applied, and notes the own
 * character's shots the prediction has not made yet (so its own copy is
 * skipped later). Returns the record's new size. */
static unsigned int leave_out_own(unsigned char *d, unsigned int size)
{
    unsigned int at = 0;
    while (at + 2 <= size) {
        unsigned int header, length, whole;
        if (d[at] == 0) {
            ++at; /* padding after a message of odd size (MsgQueue_Enqueue) */
            continue;
        }
        header = (unsigned int)d[at] | (unsigned int)d[at + 1] << 8;
        length = header >> 6;
        whole = 2 + length;
        if (at + whole > size) {
            break;
        }
        if (((header >> 1) & 31u) == SHOT_GATE && length >= SHOT_KEY && length <= SHOT_ROOM) {
            const unsigned char *b = d + at + 2;
            unsigned int n = 0;
            if (tracing()) {
                khdays_log("predict: the host's shot (host tick %u): %02x %02x %02x %02x %02x %02x %02x %02x, %u held",
                           host_tick, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], shot_count);
            }
            while (n < shot_count && !same_shot(&shots[n], b, length)) {
                ++n;
            }
            if (n == shot_count) {
                n = 0;
                while (n < shot_count && !like_shot(&shots[n], b, length)) {
                    ++n;
                }
            }
            if (n < shot_count) {
                if (memcmp(shots[n].data, b, length) != 0) {
                    ++shots_differed;
                }
                ++shots_matched;
                take_shot(shots, &shot_count, n);
                if ((length & 1u) != 0 && at + whole < size && d[at + whole] == 0) {
                    ++whole;
                }
                memmove(d + at, d + at + whole, size - at - whole);
                size -= whole;
                continue;
            }
            if (on() && !following && own_owner >= 0 && (int)((b[0] >> 2) & 3u) == own_owner) {
                keep_shot(&ahead, &ahead_count, &ahead_room, b, length);
            }
        }
        at += whole;
    }
    return size;
}

/* A predicted shot the host never made gives back the unit of budget it
 * spent. Only the host's commands refill the budget, so the guest must spend
 * exactly what the host does.
 * Layout (Ov022_RunShotCommand, Ov022_SendPlacementMessage): the actor's
 * channels at +0x2644, 0x30 bytes each, with the budget at +0x18 and its
 * maximum at +0x19, and descriptors at +0x10, 0x50 bytes each, whose flag 1
 * makes a shot free. The command's channel is in the low 3 bits of its second
 * byte, its descriptor in bits 3-5 of byte 0x16. */
static void give_back(const OwnShot *shot)
{
    unsigned int channels, descriptors, flags;
    int actor;
    unsigned char *channel;
    if ((shot->data[0] & 3u) != 0 || shot->size <= 0x16) {
        return; /* not a spawn */
    }
    actor = GetEntryField20ByIndex((shot->data[0] >> 2) & 3);
    if (actor == 0) {
        return;
    }
    memcpy(&channels, (const unsigned char *)KHDAYS_W(actor) + 0x2644, 4);
    if (channels == 0) {
        return;
    }
    channel = (unsigned char *)KHDAYS_W(channels + (shot->data[1] & 7u) * 0x30u);
    memcpy(&descriptors, channel + 0x10, 4);
    if (descriptors != 0) {
        memcpy(&flags, (const unsigned char *)KHDAYS_W(descriptors + ((shot->data[0x16] >> 3) & 7u) * 0x50u), 4);
        if ((flags & 1u) != 0) {
            return; /* a free shot spent nothing */
        }
    }
    if (channel[0x18] < channel[0x19]) {
        ++channel[0x18];
        ++shots_refunded;
    }
}

/* Forgets the own shots the host went past without making them, giving back
 * what they spent. */
static void forget_old_shots(void)
{
    unsigned int kept_shots = 0;
    for (unsigned int n = 0; n < shot_count; ++n) {
        if (shots[n].tick + 1u < host_tick) {
            ++shots_expired;
            give_back(&shots[n]);
        } else {
            shots[kept_shots++] = shots[n];
        }
    }
    shot_count = kept_shots;
}

/* Moves a position by the last correction (used by prepare.py's join). */
void khdays_mp_predict_shift(int *x, int *y, int *z)
{
    *x += shift[0];
    *y += shift[1];
    *z += shift[2];
}

/* The host's record of a character arrived (Ov022_Party_ApplyNetState).
 * For the own character, compares it with the predicted path. Returns 0 to
 * apply the host's record (the game's own way). Otherwise the prediction
 * stands (only the HP is taken from the host), and with 2 the character is
 * also moved by the correction (khdays_mp_predict_shift). */
int khdays_mp_predict_keep(int own, int actor, int record)
{
    const unsigned char *r;
    unsigned long long state;
    unsigned short hp;
    short record_animation;
    int off = 0x7fffffff, off_any = 0x7fffffff;
    unsigned int best = 0, best_any = 0;
    if (!on() || !own) {
        return 0;
    }
    r = (const unsigned char *)KHDAYS_W(record);
    /* the host's record (Ov022_SendActorStatePacket): event word at +0 (the
     * character's +0x464 on the host), position at +8, HP at +0x20 */
    memcpy(&state, r, 8);
    memcpy(&hp, r + 0x20, 2);
    memcpy(&record_animation, r + 0x1c, 2);
    if (tracing()) {
        trace_record(r);
    }
    if ((state & 0x100u) != 0 || hp == 0) {
        /* The host decided a hit, or the character is down (Ov022_ReceiveHit's flag 0x10 becomes event word
         * 0x100 for every tick it lasts, 28 in a hit): show the host's version until it is over. */
        if (!following) {
            ++reactions;
        }
        following = 1;
        path_count = 0;
        ++followed;
        return 0;
    }
    if (following) {
        if (!free_animation(record_animation)) {
            /* the host's version is still busy (an attack it began while it was
             * shown): keep showing it until it is back to an idle animation */
            ++followed;
            return 0;
        }
        following = 0;
        restarting = 1;
        unmatched_run = 0;
    }
    {
        short animation;
        unsigned short heading;
        memcpy(&animation, r + 0x1c, 2);
        memcpy(&heading, r + 0x18, 2);
        for (unsigned int i = 0; i < path_count; ++i) {
            int d = 0;
            for (int k = 0; k < 3; ++k) {
                int host, here;
                memcpy(&host, r + 8 + 4 * k, 4);
                here = path[i * STEP + (unsigned int)k];
                here = host > here ? host - here : here - host;
                d = here > d ? here : d;
            }
            if (d < off_any) {
                off_any = d;
                best_any = i;
            }
            if ((short)path[i * STEP + 3] == animation &&
                (d < off || (d == off && (unsigned short)path[best * STEP + 5] != heading &&
                             (unsigned short)path[i * STEP + 5] == heading))) {
                /* the closest step; among equally close ones, one with the host's heading */
                off = d;
                best = i;
            }
        }
    }
    ++kept;
    if (path_count == 0) {
        return 1;
    }
    if (off == 0x7fffffff) {
        /* the host shows an animation the path never showed: count it and keep the path from the closest step
         * on */
        ++unmatched;
        memmove(path, path + best_any * STEP, (size_t)(path_count - best_any) * STEP * sizeof(int));
        path_count -= best_any;
        if (++unmatched_run > lead_at_match + 3u) {
            /* this has lasted longer than the host needs to catch up, so the
             * host ran something else: show its version until it is idle, then
             * restart the prediction */
            ++diverged;
            following = 1;
            path_count = 0;
            ++followed;
            return 0;
        }
        return 1;
    }
    unmatched_run = 0;
    /* the host is past the older steps: drop them */
    memmove(path, path + best * STEP, (size_t)(path_count - best) * STEP * sizeof(int));
    path_count -= best;
    host_tick = (unsigned int)path[4];
    lead_at_match = path_count;
    forget_old_shots();
    {
        unsigned short host_heading;
        memcpy(&host_heading, r + 0x18, 2);
        if ((unsigned short)path[5] != host_heading) {
            /* the host has it facing another way: turn the character and its
             * path by the difference */
            const int difference = (int)host_heading - (int)(unsigned short)path[5];
            turn_by(actor, difference);
            for (unsigned int i = 0; i < path_count; ++i) {
                path[i * STEP + 5] = (unsigned short)(path[i * STEP + 5] + difference);
            }
            ++turned;
        }
    }
    if (off == 0) {
        return 1;
    }
    /* the host has it somewhere else: move the character and its path by the
     * difference (the host's position is the one that counts) */
    for (int k = 0; k < 3; ++k) {
        int host;
        memcpy(&host, r + 8 + 4 * k, 4);
        shift[k] = host - path[(unsigned int)k];
    }
    for (unsigned int i = 0; i < path_count; ++i) {
        for (int k = 0; k < 3; ++k) {
            path[i * STEP + (unsigned int)k] += shift[k];
        }
    }
    ++moved;
    farthest = off > farthest ? off : farthest;
    return 2;
}
