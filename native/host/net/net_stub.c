/* host/net/net_stub.h: host/net/net.h answered from a queue. */
#include "net_stub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Item {
    int tcp;
    KhdaysNetAddress from;
    unsigned char *data;
    size_t size;
} Item;

static Item *queue;
static size_t queued, taken, room;
static unsigned long long clock_ms = 1000;
static unsigned int random_state = 1;

struct KhdaysUdp {
    unsigned short port;
};

void (*khdays_stub_sent)(const KhdaysNetAddress *to, const void *data, size_t size);

void khdays_stub_reset(void)
{
    for (size_t i = taken; i < queued; ++i) {
        free(queue[i].data);
    }
    queued = taken = 0;
    clock_ms = 1000;
    random_state = 1;
}

static void push(int tcp, const KhdaysNetAddress *from, const void *data, size_t size)
{
    Item *item;
    if (queued == room) {
        const size_t bigger = room > 0 ? room * 2 : 64;
        Item *more = (Item *)realloc(queue, bigger * sizeof(*queue));
        if (more == NULL) {
            abort();
        }
        queue = more;
        room = bigger;
    }
    item = &queue[queued++];
    item->tcp = tcp;
    item->from = from != NULL ? *from : (KhdaysNetAddress){0, 0};
    item->data = (unsigned char *)malloc(size > 0 ? size : 1);
    if (item->data == NULL) {
        abort();
    }
    memcpy(item->data, data, size);
    item->size = size;
}

void khdays_stub_push_datagram(const KhdaysNetAddress *from, const void *data, size_t size)
{
    push(0, from, data, size);
}

void khdays_stub_push_tcp(const void *data, size_t size)
{
    push(1, NULL, data, size);
}

/* The next queued item of the kind asked for (earlier ones of the other
 * kind are left), or NULL. */
static Item *next(int tcp)
{
    for (size_t i = taken; i < queued; ++i) {
        if (queue[i].tcp == tcp) {
            if (i != taken) {
                const Item swap = queue[i];
                memmove(&queue[taken + 1], &queue[taken], (i - taken) * sizeof(*queue));
                queue[taken] = swap;
            }
            return &queue[taken++];
        }
    }
    return NULL;
}

int khdays_host_net_start(void)
{
    return 1;
}

KhdaysUdp *khdays_host_udp_open(unsigned short port, int shared)
{
    static unsigned short next_port = 40000;
    KhdaysUdp *udp = (KhdaysUdp *)malloc(sizeof(*udp));
    (void)shared;
    if (udp != NULL) {
        udp->port = port != 0 ? port : next_port++;
    }
    return udp;
}

unsigned short khdays_host_udp_port(const KhdaysUdp *udp)
{
    return udp->port;
}

int khdays_host_udp_join(KhdaysUdp *udp, unsigned int group)
{
    (void)udp;
    (void)group;
    return 1;
}

int khdays_host_udp_send(KhdaysUdp *udp, const KhdaysNetAddress *to, const void *data, size_t size)
{
    (void)udp;
    if (khdays_stub_sent != NULL) {
        khdays_stub_sent(to, data, size);
    }
    return 1;
}

long khdays_host_udp_receive(KhdaysUdp *udp, KhdaysNetAddress *from, void *data, size_t room_bytes)
{
    Item *item = next(0);
    size_t size;
    (void)udp;
    if (item == NULL) {
        return -1;
    }
    size = item->size < room_bytes ? item->size : room_bytes;
    memcpy(data, item->data, size);
    *from = item->from;
    free(item->data);
    item->data = NULL;
    return (long)size;
}

void khdays_host_udp_wait(KhdaysUdp *const *udps, int count, int ms)
{
    (void)udps;
    (void)count;
    clock_ms += (unsigned long long)(ms > 0 ? ms : 0); /* time passes while nothing comes */
}

void khdays_host_udp_close(KhdaysUdp *udp)
{
    free(udp);
}

int khdays_host_net_addresses(unsigned int *ips, int room_count)
{
    /* one private interface and the loopback */
    if (room_count > 0) {
        ips[0] = 0xc0a80102u; /* 192.168.1.2 */
    }
    if (room_count > 1) {
        ips[1] = 0x7f000001u;
    }
    return 2;
}

int khdays_host_net_resolve(const char *text, unsigned short port, KhdaysNetAddress *out)
{
    unsigned int a, b, c, d, p;
    const int n = sscanf(text, "%u.%u.%u.%u:%u", &a, &b, &c, &d, &p);
    if (n < 4 || a > 255 || b > 255 || c > 255 || d > 255 || (n == 5 && p > 65535)) {
        return 0; /* names resolve to nothing here */
    }
    out->ip = a << 24 | b << 16 | c << 8 | d;
    out->port = (unsigned short)(n == 5 ? p : port);
    return 1;
}

unsigned int khdays_host_build_identity(void)
{
    return 0x12345678u;
}

void khdays_host_random(void *data, size_t size)
{
    unsigned char *bytes = (unsigned char *)data;
    for (size_t i = 0; i < size; ++i) {
        random_state = random_state * 1664525u + 1013904223u;
        bytes[i] = (unsigned char)(random_state >> 24);
    }
}

unsigned long long khdays_host_net_clock_ms(void)
{
    return clock_ms;
}

long khdays_host_tcp_exchange(const KhdaysNetAddress *to, const void *request, size_t size, void *reply,
                              size_t room_bytes, int ms, unsigned int *local)
{
    Item *item = next(1);
    size_t got;
    (void)to;
    (void)ms;
    if (khdays_stub_sent != NULL) {
        khdays_stub_sent(NULL, request, size);
    }
    if (item == NULL) {
        return -1;
    }
    got = item->size < room_bytes ? item->size : room_bytes;
    memcpy(reply, item->data, got);
    free(item->data);
    item->data = NULL;
    if (local != NULL) {
        *local = 0xc0a80102u;
    }
    return (long)got;
}

int khdays_host_net_gateway(unsigned int *ip)
{
    *ip = 0xc0a80101u; /* 192.168.1.1 */
    return 1;
}

unsigned long long khdays_host_physical_memory(void)
{
    return 8ull << 30;
}
