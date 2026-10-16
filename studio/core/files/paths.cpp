#include "core/files/paths.h"

#include <algorithm>
#include <cctype>

namespace studio {

std::string u8(const fs::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

std::string native_u8(const fs::path& path) {
    fs::path p = path;
    const std::u8string text = p.make_preferred().u8string();
    return std::string(text.begin(), text.end());
}

fs::path path_u8(std::string_view utf8) {
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(c < 0x80 ? std::tolower(c) : c); });
    return out;
}

bool inside(const fs::path& dir, const fs::path& path) {
    std::error_code ec;
    fs::path d = fs::absolute(dir, ec).lexically_normal();
    fs::path p = fs::absolute(path, ec).lexically_normal();
    if (d.has_filename() == false)
        d = d.parent_path();  // a trailing separator
    if (p.has_filename() == false)
        p = p.parent_path();
    auto di = d.begin();
    auto pi = p.begin();
    for (; di != d.end(); ++di, ++pi) {
        if (pi == p.end() || *di != *pi)
            return false;
    }
    return pi != p.end();  // strictly inside
}

}  // namespace studio
