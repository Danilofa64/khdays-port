/* khdays-server: where players meet for multiplayer over the internet. Hosts register rooms, guests
 * list them, and the server introduces the two sides (relaying their datagrams if they cannot reach
 * each other). The protocol is server_core.c; this is the program around it. No player's address is
 * ever written, in the window or the log; Windows and Linux alike (docs/MULTIPLAYER.md). */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L /* localtime_r */
#endif
#include "../host/net/net.h"
#include "portmap.h"
#include "server_core.h"
#include "version.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef unsigned long long u64;

#define DEFAULT_PORT 35821
static u64 status_every_ms = 60000; /* --status */
/* The memory the server may use, unless --memory says otherwise: an eighth of
 * the machine's (the maintainer's VPS also serves a website), or 64 MB when
 * the machine's memory is not known. */
#define BUDGET_SHARE 8
#define BUDGET_UNKNOWN (64ull << 20)

static FILE *log_file;

static void say(const char *format, ...)
{
    char line[512];
    char when[32];
    const time_t now = time(NULL);
    struct tm local;
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &local);
    printf("%s  %s\n", when, line);
    fflush(stdout);
    if (log_file != NULL) {
        fprintf(log_file, "%s  %s\n", when, line);
        fflush(log_file);
    }
}

static void usage(void)
{
    printf("khdays-server [--port N] [--relay on|off] [--router on|off] [--memory MB] [--status S] [--log FILE]\n"
           "  --port N      the UDP port to listen on (default %d)\n"
           "  --relay off   only introduce players, never pass their datagrams\n"
           "  --router off  do not ask the router to open the port (NAT-PMP, UPnP)\n"
           "  --memory MB   what the server may hold (default: an eighth of this machine's memory)\n"
           "  --status S    say what it holds and did every S seconds when it changed (default 60)\n"
           "  --log FILE    also write what the window says to FILE (no player's address)\n",
           DEFAULT_PORT);
}

static void say_ip(const char *before, unsigned int ip, unsigned short port, const char *after)
{
    if (port == DEFAULT_PORT) {
        say("%s%u.%u.%u.%u%s", before, ip >> 24, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff, after);
    } else {
        say("%s%u.%u.%u.%u:%u%s", before, ip >> 24, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff, port, after);
    }
}

/* Works out how players on the internet can reach this server, and says it.
 * If the machine has a public address (a rented server), that is it.
 * Otherwise it asks the home router to open the port (unless `router` is 0)
 * and says the address players should type in the game, or why nothing could
 * be opened. */
static void reach(unsigned short port, int router, KhdaysPortmap *map)
{
    const int count = khdays_host_net_addresses(NULL, 0);
    unsigned int *ips = (unsigned int *)calloc((size_t)count, sizeof(*ips));
    unsigned int public_ip = 0;
    char why[160];
    if (ips != NULL) {
        khdays_host_net_addresses(ips, count);
        for (int i = 0; i < count - 1; ++i) { /* the loopback, last, left out */
            const int private_ip = khdays_ip_private(ips[i]);
            say("  this machine: %u.%u.%u.%u%s", ips[i] >> 24, (ips[i] >> 16) & 0xff, (ips[i] >> 8) & 0xff,
                ips[i] & 0xff, private_ip ? " (players on its local network write this)" : " (on the internet)");
            if (!private_ip && public_ip == 0) {
                public_ip = ips[i];
            }
        }
        free(ips);
    }
    memset(map, 0, sizeof(*map));
    if (public_ip != 0) {
        say_ip("this machine is on the internet itself: players write ", public_ip, port,
               " as the server (let UDP through any firewall it has)");
        return;
    }
    if (!router) {
        say("not asking the router for the port (--router off): open UDP %u on it toward this machine", port);
        return;
    }
    say("asking the router to open UDP port %u toward this machine ...", port);
    if (!khdays_portmap_open(map, port, why, sizeof(why))) {
        say("the router opened nothing: %s.", why);
        say("players outside this network reach the server only if UDP %u is open on the router toward this "
            "machine -- or run it on a machine with an address of its own on the internet",
            port);
        return;
    }
    say("the router opened UDP port %u (%s%s)", map->port, map->method == KHDAYS_PORTMAP_UPNP ? "UPnP" : "NAT-PMP",
        map->lifetime == 0 ? ", until removed" : ", asked again while the server runs");
    if (map->external == 0) {
        say("the router did not say its internet address: players write that address (a 'what is my IP' page "
            "shows it) as the server");
    } else if (khdays_ip_private(map->external)) {
        say_ip("but the router itself is behind another network (its internet address is ", map->external,
               DEFAULT_PORT, "):");
        say("the provider shares one address among many (carrier-grade NAT) and nothing can be opened from here -- "
            "run the server on a machine with an address of its own on the internet");
    } else {
        say_ip("players write ", map->external, map->port, " as the server");
    }
}

/* Says what the server keeps and what it did since the last status, but only
 * if something changed. (Counts only, no player's address.) */
static void status(KhdaysServerCounts *last)
{
    KhdaysServerCounts now;
    khdays_server_counts(&now);
    if (now.groups == last->groups && now.pairs == last->pairs && now.relayed_pairs == last->relayed_pairs &&
        now.groups_opened == last->groups_opened && now.relayed == last->relayed && now.evicted == last->evicted &&
        now.relay_dropped == last->relay_dropped && now.introductions_held == last->introductions_held &&
        now.relay_busiest == last->relay_busiest) {
        return;
    }
    say("%llu group%s open, %llu session%s through the server, %llu KB of %llu held; the busiest second of a "
        "relayed session one way: %llu datagrams", now.groups, now.groups == 1 ? "" : "s", now.relayed_pairs,
        now.relayed_pairs == 1 ? "" : "s", now.bytes >> 10, now.budget >> 10, now.relay_busiest);
    say("  since the last: %llu groups opened, %llu datagrams relayed, %llu evicted (the budget reached), "
        "%llu relayed datagrams dropped (past a session's rate), %llu introductions held back",
        now.groups_opened - last->groups_opened, now.relayed - last->relayed, now.evicted - last->evicted,
        now.relay_dropped - last->relay_dropped, now.introductions_held - last->introductions_held);
    *last = now;
}

/* Shows an error and waits for Enter, so that a window opened by double-click
 * does not close before the player can read it. */
static int fail(const char *what)
{
    fprintf(stderr, "khdays-server: %s\n", what);
    fprintf(stderr, "(press Enter to close)\n");
    (void)getchar();
    return 1;
}

int main(int argc, char **argv)
{
    unsigned short port = DEFAULT_PORT;
    static unsigned char buffer[65536];
    u64 last_status, last_tick, mapped, budget = 0;
    int router = 1, relay = 1;
    KhdaysPortmap map;
    KhdaysServerCounts said;
    KhdaysUdp *udp;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = (unsigned short)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--relay") == 0 && i + 1 < argc) {
            relay = strcmp(argv[++i], "off") != 0;
        } else if (strcmp(argv[i], "--router") == 0 && i + 1 < argc) {
            router = strcmp(argv[++i], "off") != 0;
        } else if (strcmp(argv[i], "--memory") == 0 && i + 1 < argc) {
            budget = (u64)strtoull(argv[++i], NULL, 10) << 20;
        } else if (strcmp(argv[i], "--status") == 0 && i + 1 < argc) {
            status_every_ms = (u64)strtoull(argv[++i], NULL, 10) * 1000;
        } else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            log_file = fopen(argv[++i], "a");
        } else {
            usage();
            return strcmp(argv[i], "--help") == 0 ? 0 : 2;
        }
    }
    if (!khdays_host_net_start()) {
        return fail("the system's network does not start");
    }
    udp = khdays_host_udp_open(port, 0);
    if (udp == NULL) {
        return fail("the port is taken -- is another khdays-server running? (--port N picks another)");
    }
    say("khdays-server %s: the meeting point of the port's multiplayer, on UDP port %u", KHDAYS_SERVER_VERSION, port);
    if (budget == 0) {
        const u64 machine = khdays_host_physical_memory();
        budget = machine > 0 ? machine / BUDGET_SHARE : BUDGET_UNKNOWN;
        if (machine > 0) {
            say("memory budget: %llu MB (an eighth of this machine's %llu MB; --memory sets it)", budget >> 20,
                machine >> 20);
        } else {
            say("memory budget: %llu MB (this machine's memory not known; --memory sets it)", budget >> 20);
        }
    } else {
        say("memory budget: %llu MB (--memory)", budget >> 20);
    }
    reach(port, router, &map);
    say("relaying: %s", relay ? "on" : "off");
    khdays_server_start(udp, budget, relay);
    memset(&said, 0, sizeof(said));
    last_status = last_tick = mapped = khdays_host_net_clock_ms();
    for (;;) {
        KhdaysNetAddress from;
        long got;
        u64 now;
        khdays_host_udp_wait(&udp, 1, 250);
        now = khdays_host_net_clock_ms();
        while ((got = khdays_host_udp_receive(udp, &from, buffer, sizeof(buffer))) >= 0) {
            khdays_server_datagram(&from, buffer, got, now);
        }
        if (now - last_tick >= 1000) {
            khdays_server_tick(now);
            last_tick = now;
        }
        if (map.method != KHDAYS_PORTMAP_NONE && map.lifetime != 0 && now - mapped >= map.lifetime * 500ull) {
            /* renew the router's port mapping halfway through its lifetime */
            if (!khdays_portmap_renew(&map)) {
                say("the router did not open the port again; players outside may lose the server");
            }
            mapped = now;
        }
        if (now - last_status >= status_every_ms) {
            status(&said);
            last_status = now;
        }
    }
}
