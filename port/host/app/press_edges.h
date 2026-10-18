/* Presses and releases between two reads of the pad by a game that reads it less often than they come
 * (30 times a second in the field): each press is read down at least once, and a button let go and
 * pressed again between two reads is read up once, then down, so neither a quick tap nor a quick
 * press again is lost. For the player's buttons (host/app/window.c) and the input scripts'
 * (runtime/input/input.c). */
#ifndef KHDAYS_PRESS_EDGES_H
#define KHDAYS_PRESS_EDGES_H

#include <stdint.h>

typedef struct KhdaysPressEdges {
    uint16_t reported; /* what the last read was given */
    uint16_t pressed;  /* since then: pressed */
    uint16_t released; /* since then: let go */
    uint16_t again;    /* since then: pressed after being let go */
    uint16_t pending;  /* down at the next read: let go and pressed again before the last one */
} KhdaysPressEdges;

/* The buttons went from `before` to `now`. */
static inline void khdays_press_edges_note(KhdaysPressEdges *e, uint16_t before, uint16_t now)
{
    const uint16_t down = (uint16_t)(now & ~before), up = (uint16_t)(before & ~now);
    e->again |= (uint16_t)(down & e->released);
    e->pressed |= down;
    e->released |= up;
}

/* What a read is given, the buttons held `now`: a press since the last read down even if let go, a
 * release since then up even if pressed again (that press down at the next read). */
static inline uint16_t khdays_press_edges_take(KhdaysPressEdges *e, uint16_t now)
{
    const uint16_t up_now = (uint16_t)(e->reported & e->released);
    const uint16_t out = (uint16_t)((now | e->pending | (~e->reported & e->pressed)) & ~up_now);
    e->pending = (uint16_t)(up_now & e->again);
    e->reported = out;
    e->pressed = e->released = e->again = 0;
    return out;
}

#endif
