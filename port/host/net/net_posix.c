/* The network functions of net.h on Linux and macOS: BSD sockets, and
 * getifaddrs for the list of interfaces. It behaves the same as net_win.c. */
#define _DEFAULT_SOURCE
#include "net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#endif
#if defined(__linux__)
#include <sys/sysinfo.h>
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0 /* macOS: a closed connection's signal is not asked for here */
#endif

struct KhdaysUdp {
    int socket;
    unsigned short port;
    unsigned int *interfaces; /* joined, in network order; grown as needed */
    int interface_count;
};

int khdays_host_net_start(void)
{
    return 1;
}

KhdaysUdp *khdays_host_udp_open(unsigned short port, int shared)
{
    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    const int on = 1;
    KhdaysUdp *udp;
    const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        fprintf(stderr, "net: no UDP socket (%s)\n", strerror(errno));
        return NULL;
    }
    if (shared) {
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#ifdef SO_REUSEPORT
        setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
#endif
    }
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(s, (struct sockaddr *)&address, sizeof(address)) != 0) {
        fprintf(stderr, "net: UDP port %u cannot be bound (%s)\n", port, strerror(errno));
        close(s);
        return NULL;
    }
    getsockname(s, (struct sockaddr *)&address, &length);
    udp = (KhdaysUdp *)calloc(1, sizeof(*udp));
    if (udp == NULL) {
        close(s);
        return NULL;
    }
    udp->socket = s;
    udp->port = ntohs(address.sin_port);
    return udp;
}

unsigned short khdays_host_udp_port(const KhdaysUdp *udp)
{
    return udp->port;
}

int khdays_host_net_addresses(unsigned int *ips, int room)
{
    struct ifaddrs *list = NULL;
    int count = 0;
    if (getifaddrs(&list) == 0) {
        for (struct ifaddrs *a = list; a != NULL; a = a->ifa_next) {
            if (a->ifa_addr == NULL || a->ifa_addr->sa_family != AF_INET || !(a->ifa_flags & IFF_UP) ||
                (a->ifa_flags & IFF_LOOPBACK)) {
                continue;
            }
            if (count < room) {
                ips[count] = ntohl(((struct sockaddr_in *)a->ifa_addr)->sin_addr.s_addr);
            }
            ++count;
        }
        freeifaddrs(list);
    }
    if (count < room) {
        ips[count] = INADDR_LOOPBACK;
    }
    return count + 1;
}

int khdays_host_udp_join(KhdaysUdp *udp, unsigned int group)
{
    const int known = khdays_host_net_addresses(NULL, 0);
    unsigned int *ips = (unsigned int *)calloc((size_t)known, sizeof(*ips));
    const unsigned char loop = 1, hops = 1;
    int joined = 0;
    if (ips == NULL) {
        return 0;
    }
    khdays_host_net_addresses(ips, known);
    setsockopt(udp->socket, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
    setsockopt(udp->socket, IPPROTO_IP, IP_MULTICAST_TTL, &hops, sizeof(hops));
    free(udp->interfaces);
    udp->interfaces = (unsigned int *)calloc((size_t)known, sizeof(*udp->interfaces));
    udp->interface_count = 0;
    for (int i = 0; i < known && udp->interfaces != NULL; ++i) {
        struct ip_mreq request;
        request.imr_multiaddr.s_addr = htonl(group);
        request.imr_interface.s_addr = htonl(ips[i]);
        if (setsockopt(udp->socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &request, sizeof(request)) == 0) {
            udp->interfaces[udp->interface_count++] = htonl(ips[i]);
            ++joined;
        }
    }
    free(ips);
    return joined;
}

static int is_multicast(unsigned int ip)
{
    return (ip >> 28) == 0xe;
}

int khdays_host_udp_send(KhdaysUdp *udp, const KhdaysNetAddress *to, const void *data, size_t size)
{
    struct sockaddr_in address;
    int sent = 0;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(to->ip);
    address.sin_port = htons(to->port);
    if (is_multicast(to->ip) && udp->interface_count > 0) {
        for (int i = 0; i < udp->interface_count; ++i) {
            struct in_addr interface;
            interface.s_addr = udp->interfaces[i];
            setsockopt(udp->socket, IPPROTO_IP, IP_MULTICAST_IF, &interface, sizeof(interface));
            if (sendto(udp->socket, data, size, 0, (struct sockaddr *)&address, sizeof(address)) == (ssize_t)size) {
                sent = 1;
            }
        }
        return sent;
    }
    return sendto(udp->socket, data, size, 0, (struct sockaddr *)&address, sizeof(address)) == (ssize_t)size;
}

long khdays_host_udp_receive(KhdaysUdp *udp, KhdaysNetAddress *from, void *data, size_t room)
{
    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    /* a datagram longer than `room` is cut to it (the rest is lost) */
    const ssize_t got = recvfrom(udp->socket, data, room, 0, (struct sockaddr *)&address, &length);
    if (got < 0) {
        return -1;
    }
    from->ip = ntohl(address.sin_addr.s_addr);
    from->port = ntohs(address.sin_port);
    return (long)got;
}

void khdays_host_udp_wait(KhdaysUdp *const *udps, int count, int ms)
{
    struct pollfd *fds = (struct pollfd *)calloc((size_t)(count > 0 ? count : 1), sizeof(*fds));
    int used = 0;
    if (fds == NULL) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        if (udps[i] != NULL) {
            fds[used].fd = udps[i]->socket;
            fds[used].events = POLLIN;
            ++used;
        }
    }
    if (used == 0) {
        struct timespec pause = {ms / 1000, (long)(ms % 1000) * 1000000L};
        nanosleep(&pause, NULL);
    } else {
        poll(fds, (nfds_t)used, ms);
    }
    free(fds);
}

void khdays_host_udp_close(KhdaysUdp *udp)
{
    if (udp == NULL) {
        return;
    }
    close(udp->socket);
    free(udp->interfaces);
    free(udp);
}

unsigned int khdays_host_build_identity(void)
{
    static unsigned int identity;
    static int known;
    char path[4096];
    FILE *file = NULL;
    if (known) {
        return identity;
    }
    known = 1;
    identity = 2166136261u; /* FNV-1a over the executable's bytes */
#if defined(__APPLE__)
    {
        uint32_t size = sizeof(path);
        if (_NSGetExecutablePath(path, &size) == 0) {
            file = fopen(path, "rb");
        }
    }
#else
    {
        const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (length > 0) {
            path[length] = '\0';
            file = fopen(path, "rb");
        }
    }
#endif
    if (file != NULL) {
        static unsigned char chunk[1 << 16];
        size_t got;
        while ((got = fread(chunk, 1, sizeof(chunk), file)) > 0) {
            for (size_t i = 0; i < got; ++i) {
                identity = (identity ^ chunk[i]) * 16777619u;
            }
        }
        fclose(file);
    }
    return identity;
}

void khdays_host_random(void *data, size_t size)
{
    FILE *source = fopen("/dev/urandom", "rb");
    if (source == NULL || fread(data, 1, size, source) != size) {
        unsigned char *bytes = (unsigned char *)data;
        struct timespec now;
        unsigned long long x;
        clock_gettime(CLOCK_MONOTONIC, &now);
        x = (unsigned long long)now.tv_nsec ^ ((unsigned long long)getpid() << 32);
        for (size_t i = 0; i < size; ++i) {
            x = x * 6364136223846793005ull + 1442695040888963407ull;
            bytes[i] = (unsigned char)(x >> 56);
        }
    }
    if (source != NULL) {
        fclose(source);
    }
}

int khdays_host_net_resolve(const char *text, unsigned short port, KhdaysNetAddress *out)
{
    char host[256];
    const char *colon = strrchr(text, ':');
    struct addrinfo hints, *found = NULL;
    size_t length = colon != NULL ? (size_t)(colon - text) : strlen(text);
    if (length == 0 || length >= sizeof(host)) {
        return 0;
    }
    memcpy(host, text, length);
    host[length] = '\0';
    if (colon != NULL) {
        port = (unsigned short)strtoul(colon + 1, NULL, 10);
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, NULL, &hints, &found) != 0 || found == NULL) {
        return 0;
    }
    out->ip = ntohl(((struct sockaddr_in *)found->ai_addr)->sin_addr.s_addr);
    out->port = port;
    freeaddrinfo(found);
    return 1;
}

unsigned long long khdays_host_net_clock_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (unsigned long long)now.tv_sec * 1000ull + (unsigned long long)now.tv_nsec / 1000000ull;
}

/* Waits until `s` can be read (`events` POLLIN) or written (POLLOUT), or
 * the moment `until` passes: 1 when it can. */
static int ready(int s, short events, unsigned long long until)
{
    const unsigned long long now = khdays_host_net_clock_ms();
    struct pollfd fd;
    if (now >= until) {
        return 0;
    }
    fd.fd = s;
    fd.events = events;
    fd.revents = 0;
    return poll(&fd, 1, (int)(until - now)) > 0 && (fd.revents & events) != 0;
}

long khdays_host_tcp_exchange(const KhdaysNetAddress *to, const void *request, size_t size, void *reply,
                              size_t room, int ms, unsigned int *local)
{
    const unsigned long long until = khdays_host_net_clock_ms() + (unsigned long long)ms;
    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    size_t sent = 0, got = 0;
    int error = 0;
    socklen_t error_size = sizeof(error);
    const int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) {
        return -1;
    }
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(to->ip);
    address.sin_port = htons(to->port);
    if (connect(s, (struct sockaddr *)&address, sizeof(address)) != 0 && errno != EINPROGRESS) {
        close(s);
        return -1;
    }
    if (!ready(s, POLLOUT, until) || getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &error_size) != 0 || error != 0) {
        close(s);
        return -1;
    }
    if (local != NULL && getsockname(s, (struct sockaddr *)&address, &length) == 0) {
        *local = ntohl(address.sin_addr.s_addr);
    }
    while (sent < size) {
        const ssize_t n = send(s, (const char *)request + sent, size - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += (size_t)n;
        } else if ((errno != EAGAIN && errno != EWOULDBLOCK) || !ready(s, POLLOUT, until)) {
            close(s);
            return -1;
        }
    }
    while (got < room && ready(s, POLLIN, until)) {
        const ssize_t n = recv(s, (char *)reply + got, room - got, 0);
        if (n <= 0) {
            break; /* closed (0) or failed */
        }
        got += (size_t)n;
    }
    close(s);
    return (long)got;
}

unsigned long long khdays_host_physical_memory(void)
{
#if defined(__linux__)
    struct sysinfo info;
    return sysinfo(&info) == 0 ? (unsigned long long)info.totalram * info.mem_unit : 0;
#elif defined(__APPLE__)
    unsigned long long bytes = 0;
    size_t size = sizeof(bytes);
    return sysctlbyname("hw.memsize", &bytes, &size, NULL, 0) == 0 ? bytes : 0;
#else
    return 0;
#endif
}

int khdays_host_net_gateway(unsigned int *ip)
{
#if defined(__linux__)
    /* the default route's gateway, from the kernel's routing table */
    FILE *routes = fopen("/proc/net/route", "r");
    char line[256];
    int found = 0;
    if (routes == NULL) {
        return 0;
    }
    while (!found && fgets(line, sizeof(line), routes) != NULL) {
        char name[64];
        unsigned int destination, gateway, flags;
        if (sscanf(line, "%63s %x %x %x", name, &destination, &gateway, &flags) == 4 && destination == 0 &&
            (flags & 0x2) != 0) { /* RTF_GATEWAY */
            *ip = ntohl(gateway); /* the table holds the address in network byte order */
            found = 1;
        }
    }
    fclose(routes);
    return found;
#else
    (void)ip;
    return 0; /* not known on this system yet; UPnP finds the router on its own */
#endif
}
