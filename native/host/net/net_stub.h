/* host/net/net.h without a network, for the fuzzers. The fuzz target
 * queues what arrives (from its input), what is sent goes to a hook the target
 * checks, and the clock only moves when the target says so. There is one
 * queue per process, and every socket reads from it. */
#ifndef KHDAYS_HOST_NET_STUB_H
#define KHDAYS_HOST_NET_STUB_H

#include "net.h"

/* Drops everything queued and puts the clock back to its start. */
void khdays_stub_reset(void);

/* Queues a datagram to be received (from `from`), or the answer the next TCP
 * exchange gets. */
void khdays_stub_push_datagram(const KhdaysNetAddress *from, const void *data, size_t size);
void khdays_stub_push_tcp(const void *data, size_t size);

/* Moves the clock on by `ms` milliseconds. */
void khdays_stub_advance(unsigned long long ms);

/* If set, called with every datagram sent and every TCP request (with `to`
 * NULL for TCP). */
extern void (*khdays_stub_sent)(const KhdaysNetAddress *to, const void *data, size_t size);

#endif
