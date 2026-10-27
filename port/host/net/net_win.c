/* The network functions of net.h on Windows: Winsock 2 for the sockets, IP
 * Helper for the list of interfaces. */
#include "net.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <mstcpip.h>
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef KHDAYS_UWP
#include <bcrypt.h> /* the random bytes (advapi32's RtlGenRandom is not in the UWP's API set) */
#endif

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

#ifndef KHDAYS_UWP
/* RtlGenRandom, as advapi32 exports it */
BOOLEAN NTAPI SystemFunction036(PVOID buffer, ULONG length);
#endif

struct KhdaysUdp {
    SOCKET socket;
    unsigned short port;
    unsigned int *interfaces; /* joined, in network order; grown as needed */
    int interface_count;
};

int khdays_host_net_start(void)
{
    static int started;
    WSADATA data;
    if (started) {
        return 1;
    }
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        fprintf(stderr, "net: Winsock does not start (error %d)\n", WSAGetLastError());
        return 0;
    }
    started = 1;
    return 1;
}

KhdaysUdp *khdays_host_udp_open(unsigned short port, int shared)
{
    struct sockaddr_in address;
    int length = sizeof(address);
    u_long nonblocking = 1;
    BOOL on = TRUE, off = FALSE;
    DWORD returned = 0;
    KhdaysUdp *udp;
    const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        fprintf(stderr, "net: no UDP socket (error %d)\n", WSAGetLastError());
        return NULL;
    }
    if (shared) {
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));
    }
    /* Sending to a copy that has quit gets an ICMP error back; do not let it
     * show up as an error on this socket's receives. */
    WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof(off), NULL, 0, &returned, NULL, NULL);
    ioctlsocket(s, FIONBIO, &nonblocking);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(s, (struct sockaddr *)&address, sizeof(address)) != 0) {
        fprintf(stderr, "net: UDP port %u cannot be bound (error %d)\n", port, WSAGetLastError());
        closesocket(s);
        return NULL;
    }
    getsockname(s, (struct sockaddr *)&address, &length);
    udp = (KhdaysUdp *)calloc(1, sizeof(*udp));
    if (udp == NULL) {
        closesocket(s);
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
    ULONG size = 16 * 1024;
    IP_ADAPTER_ADDRESSES *list = NULL;
    ULONG result;
    int count = 0;
    for (int attempt = 0; attempt < 4; ++attempt) {
        free(list);
        list = (IP_ADAPTER_ADDRESSES *)malloc(size);
        if (list == NULL) {
            return 0;
        }
        result = GetAdaptersAddresses(
            AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, list, &size);
        if (result != ERROR_BUFFER_OVERFLOW) {
            break;
        }
    }
    if (result == NO_ERROR) {
        for (IP_ADAPTER_ADDRESSES *a = list; a != NULL; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
                continue;
            }
            for (IP_ADAPTER_UNICAST_ADDRESS *u = a->FirstUnicastAddress; u != NULL; u = u->Next) {
                if (u->Address.lpSockaddr->sa_family == AF_INET) {
                    const unsigned int ip = ntohl(((struct sockaddr_in *)u->Address.lpSockaddr)->sin_addr.s_addr);
                    if (count < room) {
                        ips[count] = ip;
                    }
                    ++count;
                }
            }
        }
    }
    free(list);
    if (count < room) {
        ips[count] = INADDR_LOOPBACK;
    }
    return count + 1;
}

int khdays_host_udp_join(KhdaysUdp *udp, unsigned int group)
{
    const int known = khdays_host_net_addresses(NULL, 0);
    unsigned int *ips = (unsigned int *)calloc((size_t)known, sizeof(*ips));
    const DWORD loop = 1, hops = 1;
    int joined = 0;
    if (ips == NULL) {
        return 0;
    }
    khdays_host_net_addresses(ips, known);
    setsockopt(udp->socket, IPPROTO_IP, IP_MULTICAST_LOOP, (const char *)&loop, sizeof(loop));
    setsockopt(udp->socket, IPPROTO_IP, IP_MULTICAST_TTL, (const char *)&hops, sizeof(hops));
    free(udp->interfaces);
    udp->interfaces = (unsigned int *)calloc((size_t)known, sizeof(*udp->interfaces));
    udp->interface_count = 0;
    for (int i = 0; i < known && udp->interfaces != NULL; ++i) {
        struct ip_mreq request;
        request.imr_multiaddr.s_addr = htonl(group);
        request.imr_interface.s_addr = htonl(ips[i]);
        if (setsockopt(udp->socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&request, sizeof(request)) == 0) {
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
            setsockopt(udp->socket, IPPROTO_IP, IP_MULTICAST_IF, (const char *)&udp->interfaces[i],
                       sizeof(udp->interfaces[i]));
            if (sendto(udp->socket, (const char *)data, (int)size, 0, (struct sockaddr *)&address, sizeof(address)) ==
                (int)size) {
                sent = 1;
            }
        }
        return sent;
    }
    return sendto(udp->socket, (const char *)data, (int)size, 0, (struct sockaddr *)&address, sizeof(address)) ==
           (int)size;
}

long khdays_host_udp_receive(KhdaysUdp *udp, KhdaysNetAddress *from, void *data, size_t room)
{
    struct sockaddr_in address;
    int length = sizeof(address);
    const int got = recvfrom(udp->socket, (char *)data, (int)room, 0, (struct sockaddr *)&address, &length);
    if (got < 0) {
        /* WSAEMSGSIZE: the datagram was longer than `room` and was cut (the rest is lost) */
        if (WSAGetLastError() == WSAEMSGSIZE) {
            from->ip = ntohl(address.sin_addr.s_addr);
            from->port = ntohs(address.sin_port);
            return (long)room;
        }
        return -1;
    }
    from->ip = ntohl(address.sin_addr.s_addr);
    from->port = ntohs(address.sin_port);
    return got;
}

void khdays_host_udp_wait(KhdaysUdp *const *udps, int count, int ms)
{
    fd_set set;
    struct timeval timeout;
    int any = 0;
    FD_ZERO(&set);
    for (int i = 0; i < count; ++i) {
        if (udps[i] != NULL) {
            FD_SET(udps[i]->socket, &set);
            any = 1;
        }
    }
    if (!any) {
        Sleep((DWORD)ms);
        return;
    }
    timeout.tv_sec = ms / 1000;
    timeout.tv_usec = (ms % 1000) * 1000;
    select(0, &set, NULL, NULL, &timeout);
}

void khdays_host_udp_close(KhdaysUdp *udp)
{
    if (udp == NULL) {
        return;
    }
    closesocket(udp->socket);
    free(udp->interfaces);
    free(udp);
}

unsigned int khdays_host_build_identity(void)
{
    static unsigned int identity;
    static int known;
    wchar_t path[MAX_PATH];
    if (known) {
        return identity;
    }
    known = 1;
    identity = 2166136261u; /* FNV-1a over the executable's bytes */
    if (GetModuleFileNameW(NULL, path, MAX_PATH) != 0) {
        FILE *file = _wfopen(path, L"rb");
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
    }
    return identity;
}

void khdays_host_random(void *data, size_t size)
{
#ifdef KHDAYS_UWP
    if (BCryptGenRandom(NULL, (PUCHAR)data, (ULONG)size, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != STATUS_SUCCESS) {
#else
    if (!SystemFunction036(data, (ULONG)size)) {
#endif
        unsigned char *bytes = (unsigned char *)data;
        unsigned long long x = GetTickCount64() ^ ((unsigned long long)GetCurrentProcessId() << 32);
        for (size_t i = 0; i < size; ++i) {
            x = x * 6364136223846793005ull + 1442695040888963407ull;
            bytes[i] = (unsigned char)(x >> 56);
        }
    }
}

unsigned long long khdays_host_net_clock_ms(void)
{
    return GetTickCount64();
}

/* Waits until `s` can be read (`write` 0) or written (1), or until the time
 * `until` (GetTickCount64). Returns 1 if it can. */
static int ready(SOCKET s, int write, unsigned long long until)
{
    const unsigned long long now = GetTickCount64();
    fd_set set, errors;
    struct timeval timeout;
    if (now >= until) {
        return 0;
    }
    FD_ZERO(&set);
    FD_SET(s, &set);
    FD_ZERO(&errors);
    FD_SET(s, &errors);
    timeout.tv_sec = (long)((until - now) / 1000);
    timeout.tv_usec = (long)((until - now) % 1000) * 1000;
    return select(0, write ? NULL : &set, write ? &set : NULL, &errors, &timeout) > 0 && FD_ISSET(s, &set);
}

long khdays_host_tcp_exchange(const KhdaysNetAddress *to, const void *request, size_t size, void *reply, size_t room,
                              int ms, unsigned int *local)
{
    const unsigned long long until = GetTickCount64() + (unsigned long long)ms;
    struct sockaddr_in address;
    int length = sizeof(address);
    u_long nonblocking = 1;
    size_t sent = 0, got = 0;
    const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        return -1;
    }
    ioctlsocket(s, FIONBIO, &nonblocking);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(to->ip);
    address.sin_port = htons(to->port);
    if (connect(s, (struct sockaddr *)&address, sizeof(address)) != 0 && WSAGetLastError() != WSAEWOULDBLOCK) {
        closesocket(s);
        return -1;
    }
    if (!ready(s, 1, until)) {
        closesocket(s);
        return -1;
    }
    if (local != NULL && getsockname(s, (struct sockaddr *)&address, &length) == 0) {
        *local = ntohl(address.sin_addr.s_addr);
    }
    while (sent < size) {
        const int n = send(s, (const char *)request + sent, (int)(size - sent), 0);
        if (n > 0) {
            sent += (size_t)n;
        } else if (WSAGetLastError() != WSAEWOULDBLOCK || !ready(s, 1, until)) {
            closesocket(s);
            return -1;
        }
    }
    while (got < room && ready(s, 0, until)) {
        const int n = recv(s, (char *)reply + got, (int)(room - got), 0);
        if (n <= 0) {
            break; /* closed (0) or failed */
        }
        got += (size_t)n;
    }
    closesocket(s);
    return (long)got;
}

unsigned long long khdays_host_physical_memory(void)
{
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) ? status.ullTotalPhys : 0;
}

int khdays_host_net_gateway(unsigned int *ip)
{
#ifdef KHDAYS_UWP
    /* GetBestRoute is not in the UWP's API set: the multiplayer's gateway guess
     * is off, and the room's host address has to be given by hand. */
    (void)ip;
    return 0;
#else
    MIB_IPFORWARDROW route;
    /* the route the system would use to reach an internet address */
    if (GetBestRoute(htonl(0x08080808u), 0, &route) != NO_ERROR || route.dwForwardNextHop == 0) {
        return 0;
    }
    *ip = ntohl(route.dwForwardNextHop);
    return 1;
#endif
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
