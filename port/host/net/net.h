/* The operating system's network, as the multiplayer uses it:
 * IPv4 UDP datagrams, plus a multicast group on the
 * local network for announcing rooms. Only the system calls are here: the
 * Windows version is net_win.c (Winsock), the Linux one net_posix.c, and the
 * protocol itself is runtime/net/net_session.c. */
#ifndef KHDAYS_HOST_NET_H
#define KHDAYS_HOST_NET_H

#include <stddef.h>

/* An IPv4 address and port, in host byte order. */
typedef struct KhdaysNetAddress {
    unsigned int ip;
    unsigned short port;
} KhdaysNetAddress;

typedef struct KhdaysUdp KhdaysUdp;

/* Starts the system's sockets (once per process). Returns 1 on success;
 * otherwise 0, with the reason on stderr. */
int khdays_host_net_start(void);

/* Opens a non-blocking UDP socket on `port` (0: any free port) on every
 * interface. With `shared`, other sockets on this machine may use the same
 * port too (every copy listens to the announcements' port). Returns NULL on
 * failure, with the reason on stderr. */
KhdaysUdp *khdays_host_udp_open(unsigned short port, int shared);

/* The port the socket is bound to. */
unsigned short khdays_host_udp_port(const KhdaysUdp *udp);

/* Joins the multicast group `group` on every IPv4 interface, the loopback
 * included (for copies on the same PC), and sets sending to it with loopback
 * on and a single hop. Returns how many interfaces joined (0 if none). */
int khdays_host_udp_join(KhdaysUdp *udp, unsigned int group);

/* Sends one datagram to `to` (to a multicast group: out of every joined
 * interface). Returns 1 if the system took it. */
int khdays_host_udp_send(KhdaysUdp *udp, const KhdaysNetAddress *to, const void *data, size_t size);

/* Reads the next waiting datagram into `data` (cut to `room` bytes) and its
 * sender into *from. Returns its size, or -1 when none is waiting. */
long khdays_host_udp_receive(KhdaysUdp *udp, KhdaysNetAddress *from, void *data, size_t room);

/* Waits until one of the `count` sockets has a datagram, or `ms`
 * milliseconds pass. */
void khdays_host_udp_wait(KhdaysUdp *const *udps, int count, int ms);

void khdays_host_udp_close(KhdaysUdp *udp);

/* Fills `ips` with up to `room` of the machine's IPv4 addresses (the
 * loopback last). Returns how many there are. */
int khdays_host_net_addresses(unsigned int *ips, int room);

/* Turns `text` ("192.168.1.20", "host.example:33582") into an address,
 * using `port` when the text has none. Returns 1 on success. */
int khdays_host_net_resolve(const char *text, unsigned short port, KhdaysNetAddress *out);

/* A number that differs between builds of the port: a hash of the running
 * executable's bytes (computed once). Copies only play together if
 * it matches. */
unsigned int khdays_host_build_identity(void);

/* Random bytes from the system. */
void khdays_host_random(void *data, size_t size);

/* Milliseconds since some fixed moment; it never goes back. (Used by the
 * server; the game's session uses SDL's clock.) */
unsigned long long khdays_host_net_clock_ms(void);

/* One TCP request and its whole answer, like an HTTP exchange that closes
 * the connection (the server uses it to ask its router for a port,
 * server/portmap.c). It connects to `to`, sends `size` bytes of `request`,
 * and reads until the other side closes or `room` is full, all within `ms`
 * milliseconds. Returns the bytes read, or -1 if it could not connect or
 * send; *local gets this machine's address on that connection. */
long khdays_host_tcp_exchange(const KhdaysNetAddress *to, const void *request, size_t size, void *reply,
                              size_t room, int ms, unsigned int *local);

/* The default gateway (the router to the internet). Returns 1 with its IPv4
 * address in *ip, or 0 if there is none or it is not known. */
int khdays_host_net_gateway(unsigned int *ip);

/* The machine's physical memory in bytes, 0 if not known (the server's
 * budget). */
unsigned long long khdays_host_physical_memory(void);

#endif
