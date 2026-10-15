/* Fuzz test of the router code. Each input is turned into
 * the answers a router could send -- NAT-PMP datagrams, SSDP answers, HTTP
 * answers -- and a fake network hands them to khdays_portmap_open and to a
 * renewal. */
#include "../server/portmap.h"
#include "../host/net/net_stub.h"

#include <stdint.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const KhdaysNetAddress gateway = {0xc0a80101u, 5351}, router = {0xc0a80101u, 1900};
    KhdaysPortmap map;
    char why[160];
    size_t at = 0;
    khdays_stub_reset();
    khdays_stub_sent = NULL;
    /* each: kind u8 (0 NAT-PMP, 1 SSDP, 2 TCP), length u16, then the bytes */
    while (size - at >= 3) {
        const unsigned int kind = data[at] % 3;
        size_t length = (size_t)data[at + 1] | (size_t)data[at + 2] << 8;
        at += 3;
        if (length > size - at) {
            length = size - at;
        }
        if (kind == 2) {
            khdays_stub_push_tcp(data + at, length);
        } else {
            khdays_stub_push_datagram(kind == 0 ? &gateway : &router, data + at, length);
        }
        at += length;
    }
    if (khdays_portmap_open(&map, 35821, why, sizeof(why))) {
        khdays_portmap_renew(&map);
    }
    return 0;
}
