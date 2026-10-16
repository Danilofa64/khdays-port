/* The server's logic, apart from its program (server.c) so the fuzzer (fuzz_server.c) tests the same
 * code. It never passes a memory budget, takes the same time per datagram however much it keeps, and
 * logs only counts: no player's address is ever written. */
#ifndef KHDAYS_SERVER_CORE_H
#define KHDAYS_SERVER_CORE_H

#include "../host/net/net.h"

typedef struct KhdaysServerCounts {
    unsigned long long groups, pairs, relayed_pairs; /* kept now */
    unsigned long long bytes, budget;                /* memory in use, and allowed */
    /* since the start: */
    unsigned long long groups_opened, relayed, evicted, relay_dropped, introductions_held;
    unsigned long long relay_busiest; /* the most one pair relayed in one direction in one second */
} KhdaysServerCounts;

/* Starts the server: answers go out through `udp`, what it keeps stays within
 * `budget` bytes, and it relays datagrams when `relay` is set. It can be
 * started again after khdays_server_stop. */
void khdays_server_start(KhdaysUdp *udp, unsigned long long budget, int relay);

/* Forgets everything (the fuzzer starts each input afresh). */
void khdays_server_stop(void);

/* Handles one datagram of `got` bytes from `from`, at time `now` (ms). */
void khdays_server_datagram(const KhdaysNetAddress *from, const unsigned char *d, long got, unsigned long long now);

/* Forgets what has been silent too long. Call it about once a second. */
void khdays_server_tick(unsigned long long now);

void khdays_server_counts(KhdaysServerCounts *out);

#endif
