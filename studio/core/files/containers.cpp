#include "core/files/containers.h"

#include <cstring>

#include "core/files/lz.h"

#include "khdays/assets/message.h"  // lz_decompress

#include <algorithm>

namespace studio {

namespace {

std::uint32_t le16(const std::vector<std::uint8_t>& d, std::size_t at) {
    return at + 2 <= d.size() ? static_cast<std::uint32_t>(d[at] | d[at + 1] << 8) : 0;
}
std::uint32_t le32(const std::vector<std::uint8_t>& d, std::size_t at) {
    return at + 4 <= d.size() ? static_cast<std::uint32_t>(d[at] | d[at + 1] << 8 | d[at + 2] << 16 |
                                                           static_cast<std::uint32_t>(d[at + 3]) << 24)
                              : 0;
}
void put16(std::vector<std::uint8_t>& d, std::size_t at, std::uint32_t v) {
    d[at] = static_cast<std::uint8_t>(v & 0xFF);
    d[at + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
}
void put32(std::vector<std::uint8_t>& d, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) d[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
}

std::size_t align(std::size_t v, std::size_t a) {
    return (v + a - 1) / a * a;
}

}  // namespace

bool parse_p2(const std::vector<std::uint8_t>& file, P2File* out, std::string* why) {
    auto fail = [&](const char* reason) {
        if (why != nullptr) *why = reason;
        return false;
    };
    if (file.size() < 0x10 || file[0] != 'P' || file[1] != '2') return fail("not a P2 file");
    P2File p;
    const std::uint32_t n = le16(file, 2) & 0x1ff;
    p.base = le32(file, 0xc);
    const std::size_t sizes = 0x10, desc = 0x10 + ((n + 1) / 2) * 4;
    if (desc + 4 * n > p.base || p.base > file.size()) return fail("P2 tables past its data");
    p.head.assign(file.begin(), file.begin() + p.base);
    for (std::uint32_t e = 0; e < n; ++e) {
        const std::size_t at = p.base + (static_cast<std::size_t>(le16(file, sizes + 2 * e)) << 9);
        const std::uint32_t raw = le32(file, desc + 4 * e);
        const std::size_t size = raw & 0x7fffffffu;
        if (at > file.size() || size > file.size() - at) return fail("P2 entry past the file");
        P2Entry entry;
        entry.compressed = (raw & 0x80000000u) != 0;
        std::vector<std::uint8_t> bytes(file.begin() + static_cast<std::ptrdiff_t>(at),
                                        file.begin() + static_cast<std::ptrdiff_t>(at + size));
        if (entry.compressed && !bytes.empty()) {
            entry.lz = bytes[0];
            try {
                entry.data = khdays::assets::lz_decompress(bytes);
            } catch (const std::exception&) {
                return fail("P2 entry not LZ");
            }
        } else {
            entry.data = std::move(bytes);
        }
        p.entries.push_back(std::move(entry));
    }
    *out = std::move(p);
    return true;
}

std::vector<std::uint8_t> write_p2(const P2File& p) {
    std::vector<std::uint8_t> out = p.head;
    const std::size_t n = p.entries.size();
    const std::size_t sizes = 0x10, desc = 0x10 + ((n + 1) / 2) * 4;
    std::size_t at = p.base;
    for (std::size_t e = 0; e < n; ++e) {
        const P2Entry& entry = p.entries[e];
        const std::vector<std::uint8_t> stored = entry.compressed && entry.lz != 0 ? lz_compress(entry.lz, entry.data)
                                                                                  : entry.data;
        out.resize(at, 0);
        out.insert(out.end(), stored.begin(), stored.end());
        put16(out, sizes + 2 * e, static_cast<std::uint32_t>((at - p.base) >> 9));
        put32(out, desc + 4 * e, static_cast<std::uint32_t>(stored.size()) | (entry.compressed ? 0x80000000u : 0u));
        at = align(out.size(), 0x200);
    }
    out.resize(align(out.size(), 0x200), 0);  // the game's files end on a sector
    return out;
}

namespace {

bool parse_pack(const std::vector<std::uint8_t>& file, const char* magic, KaphFile* out, std::string* why) {
    auto fail = [&](const char* reason) {
        if (why != nullptr) *why = reason;
        return false;
    };
    if (file.size() < 0x28 || std::memcmp(file.data(), magic, 4) != 0) {
        return fail(std::strcmp(magic, "KAPH") == 0 ? "not a KAPH file" : "not a CAKP archive");
    }
    KaphFile k;
    std::size_t first = file.size(), tables_end = 0x28;
    for (int s = 0; s < 8; ++s) {
        const std::uint32_t off = le32(file, 8 + 4 * static_cast<std::size_t>(s));
        if (off == 0xffffffffu) continue;
        const std::uint32_t count = le32(file, off);
        if (off + 4 + 8ull * count > file.size()) return fail("KAPH section table past the file");
        tables_end = std::max<std::size_t>(tables_end, off + 4 + 8ull * count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t at = le32(file, off + 4 + 4 * i);
            const std::uint32_t size = le32(file, off + 4 + 4 * count + 4 * i);
            if (at > file.size() || size > file.size() - at) return fail("KAPH block past the file");
            KaphBlock b;
            b.section = s;
            b.index = i;
            b.offset = at;
            b.data.assign(file.begin() + at, file.begin() + at + size);
            k.blocks.push_back(std::move(b));
            first = std::min<std::size_t>(first, at);
        }
    }
    std::sort(k.blocks.begin(), k.blocks.end(), [](const KaphBlock& a, const KaphBlock& b) { return a.offset < b.offset; });
    if (tables_end > first) return fail("KAPH tables after its data (the game's own never are)");
    k.head.assign(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(first));
    *out = std::move(k);
    return true;
}

}  // namespace

bool parse_kaph(const std::vector<std::uint8_t>& file, KaphFile* out, std::string* why) {
    return parse_pack(file, "KAPH", out, why);
}

bool parse_cakp(const std::vector<std::uint8_t>& file, KaphFile* out, std::string* why) {
    return parse_pack(file, "CAKP", out, why);
}

std::vector<std::uint8_t> write_kaph(const KaphFile& k) {
    std::vector<std::uint8_t> out = k.head;
    for (const KaphBlock& b : k.blocks) {
        out.resize(align(out.size(), 0x20), 0);
        const std::uint32_t at = static_cast<std::uint32_t>(out.size());
        out.insert(out.end(), b.data.begin(), b.data.end());
        // its place and size in its section's table
        const std::uint32_t table = le32(out, 8 + 4 * static_cast<std::size_t>(b.section));
        const std::uint32_t count = le32(out, table);
        put32(out, table + 4 + 4 * b.index, at);
        put32(out, table + 4 + 4 * count + 4 * b.index, static_cast<std::uint32_t>(b.data.size()));
    }
    out.resize(align(out.size(), 0x20), 0);
    return out;
}

}  // namespace studio
