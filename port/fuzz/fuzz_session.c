/* Fuzzing the multiplayer session's reader: runtime/net/net_session.c compiled in (to reach its static
 * reader) on the stand-in network (net_stub.h), as a host, a guest or a player looking for groups;
 * the input becomes datagrams from a few addresses, the session's timer and delivery running between
 * them (docs/MULTIPLAYER.md, "Fuzzing"). */
#include "../runtime/net/net_session.c"
#include "../host/net/net_stub.h"

#include <stdint.h>

static const KhdaysNetAddress fuzz_server = {0xd4e34c1bu, 35821};

static void fuzz_setup(unsigned int role)
{
    static const unsigned char info[] = "fuzz room";
    static const unsigned char group_id[ID_SIZE] = {2, 1, 2, 1, 2, 1};
    if (net.lock == NULL) {
        net.lock = SDL_CreateMutex();
    }
    net.started = 1;
    net.session = khdays_host_udp_open(0, 0);
    net.lobby = khdays_host_udp_open(GROUP_PORT, 1);
    net.group.ip = GROUP_IP;
    net.group.port = GROUP_PORT;
    net.online = 1;
    net.server = fuzz_server;
    net.lan = 1;
    net.punch = (role & 4) == 0;
    net.build = 0x12345678u;
    memset(net.id, 7, ID_SIZE);
    net.role = NONE;
    switch (role & 3) {
    case 0:
        khdays_net_host(0x800356u, info, sizeof(info), 3);
        break;
    case 1:
    case 2: {
        /* joining a group the local network (1) or the server (2) listed */
        const KhdaysNetAddress host = {0xc0a80105u, 50000};
        unsigned char body[20 + sizeof(info)];
        memset(body, 0, sizeof(body));
        put32(body, net.build);
        put16(body + 8, host.port);
        body[10] = 1;
        put32(body + 12, 0x800356u);
        put32(body + 16, sizeof(info));
        memcpy(body + 20, info, sizeof(info));
        net.scanning = 1;
        net.scan_ggid = 0x800356u;
        room_seen(group_id, &host, (role & 3) == 1, body, sizeof(body), 0);
        khdays_net_join(group_id, "ssid", 4);
        break;
    }
    default:
        khdays_net_scan(0x800356u, 1);
        break;
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const KhdaysNetAddress senders[4] = {
        {0xc0a80105u, 50000}, {0xc0a80106u, 50001}, {0x0a000007u, 60000}, {0xd4e34c1bu, 35821}};
    static unsigned char out[0x10000];
    u64 now = 1000000000ull;
    size_t at = 1;
    KhdaysNetEvent e;
    if (size < 1) {
        return 0;
    }
    khdays_stub_reset();
    khdays_stub_sent = NULL;
    fuzz_setup(data[0]);
    /* each: who u8, time u8, length u16, then the datagram */
    while (size - at >= 4) {
        const unsigned int who = data[at], wait = data[at + 1];
        size_t length = (size_t)data[at + 2] | (size_t)data[at + 3] << 8;
        Route from;
        at += 4;
        if (length > size - at) {
            length = size - at;
        }
        from.address = senders[who & 3];
        from.relay = 0;
        now += (u64)wait * 10 * MS;
        on_datagram(&from, data + at, (long)length, now);
        if (who & 0x40) {
            tick(now);
        }
        /* the game takes what is due, and accepts whoever joins */
        while (khdays_net_next(&e, out, sizeof(out), (unsigned int)(now / (16 * MS)))) {
            if (e.kind == KHDAYS_NET_JOINING) {
                khdays_net_accept(e.aid, (who & 0x10) != 0);
            }
        }
        at += length;
    }
    khdays_net_end();
    return 0;
}
