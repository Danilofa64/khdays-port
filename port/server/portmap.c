/* Opens the server's port on the home router (see portmap.h): NAT-PMP first,
 * then UPnP. Whatever the router answers is only read within the bytes it
 * actually sent. */
#include "portmap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NATPMP_PORT 5351
#define NATPMP_LIFETIME 7200   /* seconds: RFC 6886's recommendation */
#define SSDP_GROUP 0xeffffffau /* 239.255.255.250 */
#define SSDP_PORT 1900
#define UPNP_LEASE 3600 /* seconds; renewed halfway through */
#define HTTP_MS 3000
#define REPLY_BYTES 65536

static char reply[REPLY_BYTES + 1];

int khdays_ip_private(unsigned int ip)
{
    return (ip >> 24) == 0 || (ip >> 24) == 10 || (ip >> 24) == 127 || (ip >> 20) == 0xac1 || /* 172.16/12 */
           (ip >> 16) == 0xc0a8 || (ip >> 16) == 0xa9fe || /* 192.168/16, 169.254/16 */
           (ip >> 22) == 0x191;                            /* 100.64/10, a provider's shared NAT */
}

static void say_why(char *why, size_t size, const char *text)
{
    if (why != NULL && size > 0) {
        snprintf(why, size, "%s", text);
    }
}

static void put16be(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)v;
}

static void put32be(unsigned char *p, unsigned int v)
{
    put16be(p, v >> 16);
    put16be(p + 2, v & 0xffff);
}

static unsigned int get16be(const unsigned char *p)
{
    return (unsigned int)p[0] << 8 | p[1];
}

static unsigned int get32be(const unsigned char *p)
{
    return get16be(p) << 16 | get16be(p + 2);
}

/* The address in the environment variable `name`, if it is set (the tests
 * use this to point the server at a fake router). */
static int address_from(const char *name, KhdaysNetAddress *out)
{
    const char *value = getenv(name);
    return value != NULL && value[0] != '\0' && khdays_host_net_resolve(value, 0, out);
}

/* Waits up to `ms` for a datagram from `from` (or from anyone if NULL).
 * Returns its size, or -1 if none came. */
static long receive_from(KhdaysUdp *u, const KhdaysNetAddress *from, unsigned char *data, size_t room, int ms)
{
    const unsigned long long until = khdays_host_net_clock_ms() + (unsigned long long)ms;
    while (khdays_host_net_clock_ms() < until) {
        KhdaysNetAddress sender;
        long got;
        khdays_host_udp_wait(&u, 1, 20);
        while ((got = khdays_host_udp_receive(u, &sender, data, room)) >= 0) {
            if (from == NULL || (sender.ip == from->ip && sender.port == from->port)) {
                return got;
            }
        }
    }
    return -1;
}

/* --- NAT-PMP (RFC 6886) -------------------------------------------------- */

/* Asks the gateway for its external address, then to map the UDP port.
 * Returns 1 if the port was mapped. */
static int natpmp(KhdaysPortmap *m)
{
    KhdaysUdp *u = khdays_host_udp_open(0, 0);
    unsigned char ask[12], answer[16];
    int known = 0, mapped = 0;
    if (u == NULL) {
        return 0;
    }
    /* The external address first, which also tells whether the router speaks
     * NAT-PMP at all: three tries, waiting 250, 500 and 1000 ms. */
    for (int attempt = 0, wait = 250; attempt < 3 && !known; ++attempt, wait *= 2) {
        ask[0] = 0; /* version */
        ask[1] = 0; /* opcode: the external address */
        khdays_host_udp_send(u, &m->gateway, ask, 2);
        if (receive_from(u, &m->gateway, answer, sizeof(answer), wait) >= 12 && answer[0] == 0 && answer[1] == 128 &&
            get16be(answer + 2) == 0) {
            m->external = get32be(answer + 8);
            known = 1;
        }
    }
    for (int attempt = 0, wait = 250; known && attempt < 3 && !mapped; ++attempt, wait *= 2) {
        ask[0] = 0;
        ask[1] = 1; /* opcode: map UDP */
        ask[2] = ask[3] = 0;
        put16be(ask + 4, m->internal_port);
        put16be(ask + 6, m->internal_port); /* the same port outside, if it can */
        put32be(ask + 8, NATPMP_LIFETIME);
        khdays_host_udp_send(u, &m->gateway, ask, 12);
        if (receive_from(u, &m->gateway, answer, sizeof(answer), wait) >= 16 && answer[0] == 0 && answer[1] == 129 &&
            get16be(answer + 2) == 0 && get16be(answer + 8) == m->internal_port) {
            m->port = (unsigned short)get16be(answer + 10);
            m->lifetime = get32be(answer + 12);
            mapped = 1;
        }
    }
    khdays_host_udp_close(u);
    if (mapped) {
        m->method = KHDAYS_PORTMAP_NATPMP;
    }
    return mapped;
}

/* --- just enough HTTP for UPnP -------------------------------------------- */

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

/* Finds `needle` in `hay`, ignoring case. NULL if it is not there. */
static const char *find_ci(const char *hay, const char *needle)
{
    const size_t n = strlen(needle);
    for (; *hay != '\0'; ++hay) {
        size_t i = 0;
        while (i < n && hay[i] != '\0' && lower((unsigned char)hay[i]) == lower((unsigned char)needle[i])) {
            ++i;
        }
        if (i == n) {
            return hay;
        }
    }
    return NULL;
}

/* Splits "http://host[:port][/path]" into an address and a path. */
static int parse_url(const char *url, KhdaysNetAddress *to, char *path, size_t room)
{
    char host[128];
    const char *at, *slash;
    size_t length;
    if (strncmp(url, "http://", 7) != 0) {
        return 0;
    }
    at = url + 7;
    slash = strchr(at, '/');
    length = slash != NULL ? (size_t)(slash - at) : strlen(at);
    if (length == 0 || length >= sizeof(host)) {
        return 0;
    }
    memcpy(host, at, length);
    host[length] = '\0';
    if (!khdays_host_net_resolve(host, 80, to)) {
        return 0;
    }
    snprintf(path, room, "%s", slash != NULL ? slash : "/");
    return 1;
}

/* Sends one HTTP request and reads the answer. Returns the status code, or -1
 * on failure, and points *body at the answer's body (inside `reply`, with a
 * chunked body joined back together). */
static int http(const KhdaysNetAddress *to, const char *request, char **body, unsigned int *local)
{
    long got = khdays_host_tcp_exchange(to, request, strlen(request), reply, REPLY_BYTES, HTTP_MS, local);
    char *end, *status;
    if (got <= 0) {
        return -1;
    }
    reply[got] = '\0';
    status = strchr(reply, ' ');
    end = strstr(reply, "\r\n\r\n");
    if (strncmp(reply, "HTTP/", 5) != 0 || status == NULL || end == NULL) {
        return -1;
    }
    *body = end + 4;
    {
        /* a chunked body: join the chunks in place */
        const char *chunked = find_ci(reply, "transfer-encoding: chunked");
        if (chunked != NULL && chunked < end) {
            char *in = *body, *out = *body;
            for (;;) {
                const unsigned long size = strtoul(in, NULL, 16);
                char *line_end = strstr(in, "\r\n");
                if (line_end == NULL || size == 0 || size > (unsigned long)(reply + got - (line_end + 2))) {
                    break;
                }
                memmove(out, line_end + 2, size);
                out += size;
                in = line_end + 2 + size;
                if (strncmp(in, "\r\n", 2) == 0) {
                    in += 2;
                }
            }
            *out = '\0';
        }
    }
    return atoi(status + 1);
}

/* Copies the text of the first <tag>...</tag> between `from` and `end` into
 * `out`. Returns 0 if there is none. */
static int xml_value(const char *from, const char *end, const char *tag, char *out, size_t room)
{
    char open[64], close[64];
    const char *a, *b;
    snprintf(open, sizeof(open), "<%s>", tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    a = strstr(from, open);
    if (a == NULL || (end != NULL && a >= end)) {
        return 0;
    }
    a += strlen(open);
    b = strstr(a, close);
    if (b == NULL || (end != NULL && b > end) || (size_t)(b - a) >= room) {
        return 0;
    }
    memcpy(out, a, (size_t)(b - a));
    out[b - a] = '\0';
    return 1;
}

/* --- UPnP's Internet Gateway Device -------------------------------------- */

/* Calls a SOAP action on the router's service. Returns the HTTP status and
 * points *body at the answer. It also learns this machine's address as the
 * router sees it (m->internal_ip). */
static int soap(KhdaysPortmap *m, const char *action, const char *arguments, char **body)
{
    static char envelope[2048], request[4096];
    const unsigned int ip = m->control.ip;
    snprintf(envelope, sizeof(envelope),
             "<?xml version=\"1.0\"?>\r\n"
             "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
             "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
             "<u:%s xmlns:u=\"%s\">%s</u:%s></s:Body></s:Envelope>\r\n",
             action, m->service, arguments, action);
    snprintf(request, sizeof(request),
             "POST %s HTTP/1.1\r\nHost: %u.%u.%u.%u:%u\r\nContent-Type: text/xml; charset=\"utf-8\"\r\n"
             "SOAPAction: \"%s#%s\"\r\nContent-Length: %u\r\nConnection: close\r\n\r\n%s",
             m->control_path, ip >> 24, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff, m->control.port, m->service,
             action, (unsigned int)strlen(envelope), envelope);
    return http(&m->control, request, body, &m->internal_ip);
}

/* Asks the router to map the port for `lease` seconds (0: forever). */
static int upnp_map(KhdaysPortmap *m, unsigned int lease)
{
    char arguments[1024];
    char *body = NULL;
    int status;
    const unsigned int ip = m->internal_ip;
    snprintf(arguments, sizeof(arguments),
             "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>UDP</NewProtocol>"
             "<NewInternalPort>%u</NewInternalPort><NewInternalClient>%u.%u.%u.%u</NewInternalClient>"
             "<NewEnabled>1</NewEnabled><NewPortMappingDescription>khdays-server</NewPortMappingDescription>"
             "<NewLeaseDuration>%u</NewLeaseDuration>",
             m->internal_port, m->internal_port, ip >> 24, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff, lease);
    status = soap(m, "AddPortMapping", arguments, &body);
    if (status == 500 && lease != 0 && body != NULL) {
        char code[16];
        /* error 725: this router only accepts mappings that never expire */
        if (xml_value(body, NULL, "errorCode", code, sizeof(code)) && atoi(code) == 725) {
            return upnp_map(m, 0);
        }
    }
    if (status == 200) {
        m->port = m->internal_port;
        m->lifetime = lease;
        return 1;
    }
    return 0;
}

/* Reads the router description at `location`, finds its WAN connection
 * service, gets the external address and maps the port. Returns 1 on
 * success. */
static int upnp_device(KhdaysPortmap *m, const char *location)
{
    static char request[1024];
    KhdaysNetAddress at;
    /* one byte shorter than m->control_path, to leave room for the "/" added
     * to a relative control URL */
    char path[256], control[sizeof(m->control_path) - 1], type[96], address[32];
    char *body = NULL;
    const char *service;
    int found = 0;
    if (!parse_url(location, &at, path, sizeof(path))) {
        return 0;
    }
    snprintf(request, sizeof(request), "GET %s HTTP/1.1\r\nHost: %u.%u.%u.%u:%u\r\nConnection: close\r\n\r\n", path,
             at.ip >> 24, (at.ip >> 16) & 0xff, (at.ip >> 8) & 0xff, at.ip & 0xff, at.port);
    if (http(&at, request, &body, NULL) != 200) {
        return 0;
    }
    /* look for a WAN IP connection first, then a PPP one */
    for (int pass = 0; pass < 2 && !found; ++pass) {
        for (service = strstr(body, "<service>"); service != NULL && !found;
             service = strstr(service + 9, "<service>")) {
            const char *end = strstr(service, "</service>");
            if (end != NULL && xml_value(service, end, "serviceType", type, sizeof(type)) &&
                strstr(type, pass == 0 ? ":WANIPConnection:" : ":WANPPPConnection:") != NULL &&
                xml_value(service, end, "controlURL", control, sizeof(control))) {
                found = 1;
            }
        }
    }
    if (!found) {
        return 0;
    }
    snprintf(m->service, sizeof(m->service), "%s", type);
    if (strncmp(control, "http://", 7) == 0) {
        if (!parse_url(control, &m->control, m->control_path, sizeof(m->control_path))) {
            return 0;
        }
    } else {
        m->control = at;
        snprintf(m->control_path, sizeof(m->control_path), "%s%s", control[0] == '/' ? "" : "/", control);
    }
    if (soap(m, "GetExternalIPAddress", "", &body) == 200 &&
        xml_value(body, NULL, "NewExternalIPAddress", address, sizeof(address))) {
        KhdaysNetAddress external;
        if (khdays_host_net_resolve(address, 0, &external)) {
            m->external = external.ip;
        }
    }
    if (m->internal_ip == 0 || !upnp_map(m, UPNP_LEASE)) {
        return 0;
    }
    m->method = KHDAYS_PORTMAP_UPNP;
    return 1;
}

/* Searches the local network for routers (SSDP) and tries each one that
 * answers, until one maps the port. */
static int upnp(KhdaysPortmap *m)
{
    static const char *const targets[] = {"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
                                          "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
                                          "urn:schemas-upnp-org:service:WANIPConnection:1"};
    KhdaysUdp *u = khdays_host_udp_open(0, 0);
    KhdaysNetAddress group = {SSDP_GROUP, SSDP_PORT};
    char tried[8][256];
    int count = 0, done = 0;
    unsigned long long until;
    if (u == NULL) {
        return 0;
    }
    address_from("KHDAYS_SERVER_SSDP", &group);
    for (size_t t = 0; t < sizeof(targets) / sizeof(targets[0]); ++t) {
        char search[256];
        snprintf(
            search, sizeof(search),
            "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: %s\r\n\r\n",
            targets[t]);
        khdays_host_udp_send(u, &group, search, strlen(search));
    }
    until = khdays_host_net_clock_ms() + 2500;
    while (!done && khdays_host_net_clock_ms() < until) {
        static unsigned char answer[2048];
        const long got = receive_from(u, NULL, answer, sizeof(answer) - 1, (int)(until - khdays_host_net_clock_ms()));
        const char *location, *end;
        char url[256];
        int seen = 0;
        if (got < 0) {
            break;
        }
        answer[got] = '\0';
        location = find_ci((const char *)answer, "\nlocation:");
        if (location == NULL) {
            continue;
        }
        location += 10;
        while (*location == ' ') {
            ++location;
        }
        end = location + strcspn(location, "\r\n");
        if ((size_t)(end - location) >= sizeof(url)) {
            continue;
        }
        memcpy(url, location, (size_t)(end - location));
        url[end - location] = '\0';
        for (int i = 0; i < count; ++i) {
            seen |= strcmp(tried[i], url) == 0;
        }
        if (seen || count == (int)(sizeof(tried) / sizeof(tried[0]))) {
            continue;
        }
        snprintf(tried[count++], sizeof(tried[0]), "%s", url);
        done = upnp_device(m, url);
    }
    khdays_host_udp_close(u);
    return done;
}

/* --- public functions ------------------------------------------------------ */

int khdays_portmap_open(KhdaysPortmap *m, unsigned short port, char *why, size_t why_size)
{
    unsigned int gateway;
    memset(m, 0, sizeof(*m));
    m->internal_port = port;
    if (address_from("KHDAYS_SERVER_GATEWAY", &m->gateway)) {
        /* a fake gateway, for the tests */
    } else if (khdays_host_net_gateway(&gateway)) {
        m->gateway.ip = gateway;
        m->gateway.port = NATPMP_PORT;
    }
    if (m->gateway.ip != 0 && natpmp(m)) {
        return 1;
    }
    if (upnp(m)) {
        return 1;
    }
    say_why(why, why_size, "no router answered NAT-PMP or UPnP (it may have them off)");
    return 0;
}

int khdays_portmap_renew(KhdaysPortmap *m)
{
    switch (m->method) {
    case KHDAYS_PORTMAP_NATPMP: return natpmp(m);
    case KHDAYS_PORTMAP_UPNP: return upnp_map(m, m->lifetime);
    default: return 0;
    }
}
