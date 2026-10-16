#include "test.h"

#include <chrono>
#include <cstdio>
#include <fstream>

namespace studio_test {

namespace {

int failures = 0;
const char* current = "";

std::filesystem::path root() {
    static const std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("khdays-studio-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    return dir;
}

}  // namespace

std::vector<Case>& cases() {
    static std::vector<Case> all;
    return all;
}

void fail(const char* file, int line, const char* what) {
    std::fprintf(stderr, "%s: %s(%d): failed: %s\n", current, file, line, what);
    ++failures;
}

std::filesystem::path scratch(const std::string& name) {
    const auto dir = root() / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void write(const std::filesystem::path& file, const std::string& bytes) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace studio_test

int main() {
    using namespace studio_test;
    for (const auto& c : cases()) {
        current = c.name;
        const int before = failures;
        c.run();
        std::printf("%s %s\n", failures == before ? "ok  " : "FAIL", c.name);
    }
    std::error_code ec;
    std::filesystem::remove_all(root(), ec);
    std::printf("%zu tests, %d failed checks\n", cases().size(), failures);
    return failures == 0 ? 0 : 1;
}
