/* NitroSDK DGT's SHA-1 block transform, which the decompilation keeps as ARM
 * assembly (libs/nitro/dgt/asm_stubs/auto/DGTi_Hash2ProcessBlock.c,
 * 0x0200bd4c), translated. The game reaches it through the DGT library's
 * known-answer table (data_020422d0).
 *
 * Each 64-byte block of `src`, its words read big-endian, is folded into the
 * five-word state at `state` with SHA-1's 80 rounds (choice, parity,
 * majority, parity), using the constants held in the 0x14 bytes before the
 * routine (0x5a827999, 0x6ed9eba1, 0x8f1bbcdc, 0xca62c1d6). The routine
 * checks the length after each block, signed, so it always hashes at least
 * one block. */
#include "hal.h"

static u32 rotl(u32 x, int n)
{
    return x << n | x >> (32 - n);
}

void DGTi_Hash2ProcessBlock(u32 *state, const u8 *src, u32 len)
{
    do {
        u32 w[80];
        u32 a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
        for (int t = 0; t < 16; ++t) {
            w[t] = (u32)src[4 * t] << 24 | (u32)src[4 * t + 1] << 16 | (u32)src[4 * t + 2] << 8 | src[4 * t + 3];
        }
        for (int t = 16; t < 80; ++t) {
            w[t] = rotl(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
        }
        for (int t = 0; t < 80; ++t) {
            u32 f, k, next;
            if (t < 20) {
                f = ((c ^ d) & b) ^ d;
                k = 0x5a827999u;
            } else if (t < 40) {
                f = b ^ c ^ d;
                k = 0x6ed9eba1u;
            } else if (t < 60) {
                f = ((b | c) & d) | (b & c);
                k = 0x8f1bbcdcu;
            } else {
                f = b ^ c ^ d;
                k = 0xca62c1d6u;
            }
            next = rotl(a, 5) + f + e + k + w[t];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = next;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        src += 64;
        len -= 64;
    } while ((s32)len > 0);
}
