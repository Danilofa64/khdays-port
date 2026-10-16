/* Opens the server's port on the home router, so nobody has to do it by hand.
 * It tries NAT-PMP (RFC 6886) first, then UPnP (found with SSDP, asked with SOAP). A machine
 * with a public address needs none of this. */
#ifndef KHDAYS_SERVER_PORTMAP_H
#define KHDAYS_SERVER_PORTMAP_H

#include "../host/net/net.h"

enum { KHDAYS_PORTMAP_NONE, KHDAYS_PORTMAP_NATPMP, KHDAYS_PORTMAP_UPNP };

typedef struct KhdaysPortmap {
    int method;            /* KHDAYS_PORTMAP_* */
    unsigned int external; /* the router's internet address, 0 if not known */
    unsigned short port;   /* the port the internet reaches, UDP */
    unsigned short internal_port;
    unsigned int lifetime; /* seconds the router keeps it (0: until removed) */
    /* NAT-PMP: the gateway. UPnP: the router's control URL and service. */
    KhdaysNetAddress gateway;
    KhdaysNetAddress control;
    char control_path[256];
    char service[96];
    unsigned int internal_ip; /* this machine, as the router knows it */
} KhdaysPortmap;

/* Opens UDP `port` on the router. Returns 1 on success (*m says how, and the
 * router's internet address if it gave it), or 0 with the reason in `why`.
 * For the tests, KHDAYS_SERVER_GATEWAY and KHDAYS_SERVER_SSDP (ip:port)
 * replace the real gateway and SSDP address. */
int khdays_portmap_open(KhdaysPortmap *m, unsigned short port, char *why, size_t why_size);

/* Renews the mapping before it expires. Returns 1 if the router agreed. */
int khdays_portmap_renew(KhdaysPortmap *m);

/* Whether the internet cannot reach `ip`: a private, carrier-grade NAT,
 * link-local or loopback address. */
int khdays_ip_private(unsigned int ip);

#endif
