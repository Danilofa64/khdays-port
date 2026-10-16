/* Fuzz test of the server's logic.
 *
 * Each input is turned into a series of datagrams: each comes from one of a
 * few addresses (or a forged one) after some time, with a valid header or
 * with raw bytes. They are fed to the server's core with a small memory
 * budget, on a fake network. Besides AddressSanitizer's checks, the run
 * stops if one of two rules breaks: an answer is never longer than the
 * datagram that asked, and the memory in use never
 * passes the budget. */
#include "../server/server_core.h"
#include "../host/net/net_stub.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER 16
#define BUDGET 16384 /* small: the eviction is reached within one input */

static KhdaysNetAddress current;
static size_t current_size;

static void sent(const KhdaysNetAddress *to, const void *data, size_t size)
{
    (void)data;
    if (to != NULL && to->ip == current.ip && to->port == current.port && size > current_size) {
        fprintf(stderr, "fuzz: an answer of %u bytes to a datagram of %u\n", (unsigned int)size,
                (unsigned int)current_size);
        abort();
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const KhdaysNetAddress senders[4] = {
        {0xc6336401u, 40000}, {0xc6336401u, 40001}, {0xcb007102u, 50000}, {0x0a000005u, 60000}};
    static unsigned char datagram[HEADER + 65535];
    KhdaysServerCounts counts;
    unsigned long long now = 1000;
    size_t at = 0;
    khdays_stub_reset();
    khdays_stub_sent = sent;
    khdays_server_start(khdays_host_udp_open(35821, 0), BUDGET, 1);
    /* each: who u8, time u8, length u16, then the bytes */
    while (size - at >= 4) {
        const unsigned int who = data[at], wait = data[at + 1];
        size_t length = (size_t)data[at + 2] | (size_t)data[at + 3] << 8;
        at += 4;
        if (length > size - at) {
            length = size - at;
        }
        if (who & 0x80) {
            /* a forged sender: its address from the input */
            current.ip = 0x0b000000u | (unsigned int)(length * 2654435761u >> 8);
            current.port = (unsigned short)(1024 + wait * 7);
        } else {
            current = senders[who & 3];
        }
        now += (unsigned long long)wait * 10;
        if ((who & 0x20) == 0 && length >= 1) {
            /* the header made here: the kind from the input, the body after it */
            const size_t body = length - 1;
            memcpy(datagram, "KHD1", 4);
            datagram[4] = data[at];
            datagram[5] = 1;
            datagram[6] = (unsigned char)body;
            datagram[7] = (unsigned char)(body >> 8);
            memset(datagram + 8, (int)(who & 0x0f), 6);
            datagram[14] = datagram[15] = 0;
            memcpy(datagram + HEADER, data + at + 1, body);
            current_size = HEADER + body;
            khdays_server_datagram(&current, datagram, (long)current_size, now);
        } else {
            current_size = length;
            khdays_server_datagram(&current, data + at, (long)length, now);
        }
        if (who & 0x40) {
            khdays_server_tick(now);
        }
        khdays_server_counts(&counts);
        if (counts.bytes > counts.budget) {
            fprintf(stderr, "fuzz: %llu bytes held, the budget %llu\n", counts.bytes, counts.budget);
            abort();
        }
        at += length;
    }
    khdays_server_stop();
    return 0;
}
