/* SHA-1 (sha1.h). */
#include "sha1.h"

#include <string.h>

typedef unsigned int u32;

static u32 rotl(u32 x, int n)
{
    return x << n | x >> (32 - n);
}

static void sha1_block(u32 h[5], const unsigned char *p)
{
    u32 w[80], a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int t = 0; t < 16; ++t) {
        w[t] = (u32)p[4 * t] << 24 | (u32)p[4 * t + 1] << 16 | (u32)p[4 * t + 2] << 8 | p[4 * t + 3];
    }
    for (int t = 16; t < 80; ++t) {
        w[t] = rotl(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
    }
    for (int t = 0; t < 80; ++t) {
        const u32 f = t < 20 ? (b & c) | (~b & d) : t < 40 ? b ^ c ^ d : t < 60 ? (b & c) | (b & d) | (c & d) : b ^ c ^ d;
        const u32 k = t < 20 ? 0x5a827999u : t < 40 ? 0x6ed9eba1u : t < 60 ? 0x8f1bbcdcu : 0xca62c1d6u;
        const u32 next = rotl(a, 5) + f + e + k + w[t];
        e = d;
        d = c;
        c = rotl(b, 30);
        b = a;
        a = next;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

void khdays_sha1(const unsigned char *data, unsigned int len, unsigned char digest[20])
{
    u32 h[5] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u};
    unsigned char tail[128];
    u32 done = 0, rest;
    const unsigned long long bits = (unsigned long long)len * 8u;
    for (; len - done >= 64; done += 64) {
        sha1_block(h, data + done);
    }
    rest = len - done;
    memset(tail, 0, sizeof(tail));
    memcpy(tail, data + done, rest);
    tail[rest] = 0x80;
    rest = rest + 1 + 8 <= 64 ? 64 : 128;
    for (int i = 0; i < 8; ++i) {
        tail[rest - 1 - i] = (unsigned char)(bits >> (8 * i));
    }
    sha1_block(h, tail);
    if (rest == 128) {
        sha1_block(h, tail + 64);
    }
    for (int i = 0; i < 5; ++i) {
        digest[4 * i] = (unsigned char)(h[i] >> 24);
        digest[4 * i + 1] = (unsigned char)(h[i] >> 16);
        digest[4 * i + 2] = (unsigned char)(h[i] >> 8);
        digest[4 * i + 3] = (unsigned char)h[i];
    }
}
