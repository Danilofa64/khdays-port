/* host/app/release_version.h. */
#include "release_version.h"

#include <ctype.h>
#include <string.h>

/* Reads a version: its three numbers (missing ones 0) and whether it is a pre-release. 0 when `s`
 * is no version. */
static int parse(const char *s, unsigned long n[3], int *pre)
{
    n[0] = n[1] = n[2] = 0;
    *pre = 0;
    if (s == NULL) {
        return 0;
    }
    if (*s == 'v' || *s == 'V') {
        ++s;
    }
    for (int i = 0; i < 3; ++i) {
        unsigned long v = 0;
        if (!isdigit((unsigned char)*s)) {
            return 0;
        }
        while (isdigit((unsigned char)*s)) {
            v = v * 10 + (unsigned long)(*s - '0');
            ++s;
        }
        n[i] = v;
        if (*s != '.' || i == 2) {
            break;
        }
        ++s;
    }
    if (*s == '-') {
        *pre = s[1] != '\0';
        return *pre;
    }
    return *s == '\0' || *s == '+';
}

int khdays_version_valid(const char *s)
{
    unsigned long n[3];
    int pre;
    return parse(s, n, &pre);
}

int khdays_version_compare(const char *a, const char *b)
{
    unsigned long x[3], y[3];
    int pa, pb;
    const int va = parse(a, x, &pa), vb = parse(b, y, &pb);
    if (!va || !vb) {
        return va - vb;
    }
    for (int i = 0; i < 3; ++i) {
        if (x[i] != y[i]) {
            return x[i] < y[i] ? -1 : 1;
        }
    }
    return pb - pa;
}

static const char *skip_space(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        ++p;
    }
    return p;
}

int khdays_release_tag(const char *json, char *tag, size_t size)
{
    static const char key[] = "\"tag_name\"";
    const char *p = json != NULL ? strstr(json, key) : NULL;
    size_t length = 0;
    if (p == NULL || tag == NULL || size == 0) {
        return 0;
    }
    p = skip_space(p + sizeof(key) - 1);
    if (*p != ':') {
        return 0;
    }
    p = skip_space(p + 1);
    if (*p != '"') {
        return 0;
    }
    ++p;
    /* a version's characters only: the tag goes into the player's text and a web address */
    while (p[length] != '"') {
        const char c = p[length];
        if (!(isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_' || c == '+')) {
            return 0;
        }
        ++length;
    }
    if (length == 0 || length + 1 > size) {
        return 0;
    }
    memcpy(tag, p, length);
    tag[length] = '\0';
    return khdays_version_valid(tag);
}
