#include "core/files/file_write.h"

#include "core/files/paths.h"

#include <fstream>
#include <vector>

#ifdef _WIN32
#include <winerror.h>
#endif

namespace studio {

std::string copy_bytes(const fs::path& from, const fs::path& to) {
    std::ifstream in(from, std::ios::binary);
    if (!in) return "cannot read " + u8(from);
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!out) return "cannot write " + u8(to);
    std::vector<char> buffer(1 << 20);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize n = in.gcount();
        if (n > 0) out.write(buffer.data(), n);
        if (!out) return "cannot write " + u8(to);
    }
    if (in.bad()) return "cannot read " + u8(from);
    return {};
}

WriteResult write_into_mod(const fs::path& mod_dir, const fs::path& destination,
                           const std::function<std::string(const fs::path&)>& produce,
                           const std::function<std::string(const fs::path&)>& verify) {
    WriteResult r;
    if (!inside(mod_dir, destination)) {
        r.outside = true;
        r.why = u8(destination) + " is not inside " + u8(mod_dir);
        return r;
    }
    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    if (ec) {
        r.why = "cannot make " + u8(destination.parent_path()) + ": " + ec.message();
        return r;
    }
    fs::path temporary = destination.parent_path() / destination.stem();
    temporary += ".studio-tmp";
    temporary += destination.extension();

    auto drop_temporary = [&] {
        std::error_code ignored;
        fs::remove(temporary, ignored);
    };
    if (std::string why = produce(temporary); !why.empty()) {
        drop_temporary();
        r.why = why;
        return r;
    }
    if (verify) {
        if (std::string why = verify(temporary); !why.empty()) {
            drop_temporary();
            r.why = why;
            return r;
        }
    }
    fs::rename(temporary, destination, ec);
    if (ec) {
        drop_temporary();
#ifdef _WIN32
        r.locked = ec.value() == ERROR_SHARING_VIOLATION || ec.value() == ERROR_ACCESS_DENIED ||
                   ec.value() == ERROR_LOCK_VIOLATION;
#endif
        r.why = "cannot put " + u8(destination) + " in place: " + ec.message();
        return r;
    }
    r.ok = true;
    return r;
}

}  // namespace studio
