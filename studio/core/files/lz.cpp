#include "core/files/lz.h"

#include <algorithm>

namespace studio {

std::vector<std::uint8_t> lz_compress(std::uint8_t type, const std::vector<std::uint8_t>& in) {
    std::vector<std::uint8_t> out;
    const std::size_t n = in.size();
    out.push_back(type);
    if (n > 0 && n <= 0xFFFFFF) {
        out.push_back(static_cast<std::uint8_t>(n & 0xFF));
        out.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((n >> 16) & 0xFF));
    } else {  // the extended form (a size of 0 reads as it too): three zero bytes, then the size in 32 bits
        out.insert(out.end(), {0, 0, 0});
        for (int i = 0; i < 4; ++i)
            out.push_back(static_cast<std::uint8_t>((n >> (8 * i)) & 0xFF));
    }

    // the latest positions of each 3-byte prefix, chained, within the 4096-byte window
    constexpr std::size_t kWindow = 4096;
    const std::size_t max_length = type == 0x10 ? 18 : 0xFFFF + 0x111;
    std::vector<std::int64_t> head(1 << 16, -1), prev(n, -1);
    auto key = [&](std::size_t i) {
        return ((static_cast<unsigned>(in[i]) << 8) ^ (static_cast<unsigned>(in[i + 1]) << 4) ^ in[i + 2]) & 0xFFFF;
    };

    std::size_t pos = 0, flags_at = 0;
    int bit = 8;
    while (pos < n) {
        if (bit == 8) {
            flags_at = out.size();
            out.push_back(0);
            bit = 0;
        }
        std::size_t best_len = 0, best_dist = 0;
        if (pos + 3 <= n) {
            int steps = 0;
            for (std::int64_t cand = head[key(pos)];
                 cand >= 0 && pos - static_cast<std::size_t>(cand) <= kWindow && steps < 512;
                 cand = prev[static_cast<std::size_t>(cand)], ++steps) {
                const std::size_t c = static_cast<std::size_t>(cand);
                const std::size_t limit = std::min(max_length, n - pos);
                std::size_t len = 0;
                while (len < limit && in[c + len] == in[pos + len])
                    ++len;
                if (len > best_len) {
                    best_len = len;
                    best_dist = pos - c;
                    if (len == limit)
                        break;
                }
            }
        }
        std::size_t advance = 1;
        if (best_len >= 3) {
            out[flags_at] = static_cast<std::uint8_t>(out[flags_at] | (0x80 >> bit));
            const std::size_t d = best_dist - 1;
            if (type == 0x10) {
                out.push_back(static_cast<std::uint8_t>(((best_len - 3) << 4) | (d >> 8)));
                out.push_back(static_cast<std::uint8_t>(d & 0xFF));
            } else if (best_len <= 16) {
                out.push_back(static_cast<std::uint8_t>(((best_len - 1) << 4) | (d >> 8)));
                out.push_back(static_cast<std::uint8_t>(d & 0xFF));
            } else if (best_len <= 0x110) {
                const std::size_t l = best_len - 0x11;
                out.push_back(static_cast<std::uint8_t>(l >> 4));
                out.push_back(static_cast<std::uint8_t>(((l & 0xF) << 4) | (d >> 8)));
                out.push_back(static_cast<std::uint8_t>(d & 0xFF));
            } else {
                const std::size_t l = best_len - 0x111;
                out.push_back(static_cast<std::uint8_t>(0x10 | (l >> 12)));
                out.push_back(static_cast<std::uint8_t>((l >> 4) & 0xFF));
                out.push_back(static_cast<std::uint8_t>(((l & 0xF) << 4) | (d >> 8)));
                out.push_back(static_cast<std::uint8_t>(d & 0xFF));
            }
            advance = best_len;
        } else {
            out.push_back(in[pos]);
        }
        for (std::size_t i = 0; i < advance; ++i, ++pos) {
            if (pos + 3 <= n) {
                const unsigned k = key(pos);
                prev[pos] = head[k];
                head[k] = static_cast<std::int64_t>(pos);
            }
        }
        ++bit;
    }
    return out;  // unpadded: the game's own .z files end anywhere (sizes of every remainder modulo 4)
}

}  // namespace studio
